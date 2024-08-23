/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/**************
***************
**************/

#include <config.h>
#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <vector>

#include <glib.h>
#include <glib/gi18n.h>
extern "C" {
  #include <unistd.h>
}

#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/time-elapsed.h>

#include <pan/usenet-utils/numbers.h>

#include "data-impl.h"

using namespace pan;

/**
***
**/

namespace
{
bool parse_newsrc_line(StringView const &line,
                       StringView &setme_group_name,
                       bool &setme_subscribed,
                       StringView &setme_number_ranges)
{
  bool ok(false);
  // since most groups will be unread, it's faster to test the end of the line
  // before calling strpbrk
  char const *delimiter(
    (! line.empty() && (line.back() == '!' || line.back() == ':')) ?
      &line.back() :
      line.strpbrk("!:"));
  if (delimiter)
  {
    StringView myline(line);

    setme_subscribed = *delimiter == ':';

    myline.substr(NULL, delimiter, setme_group_name);
    setme_group_name.trim();

    myline.substr(delimiter + 1, NULL, setme_number_ranges);
    setme_number_ranges.trim();

    ok = ! setme_group_name.empty();
  }

  return ok;
  }
}

// detect std::lib
#include <ciso646>
#ifdef _LIBCPP_VERSION
// using libc++
#include <algorithm>
#else
// using libstdc++
#include <ext/algorithm>
#endif

void DataImpl ::load_newsrc(Quark const &server,
                            LineReader *in,
                            alpha_groups_t &sub,
                            alpha_groups_t &unsub)
{
  Server * s = find_server (server);
  if (!s) {
    Log::add_err_va (_("Skipping newsrc file for server \"%s\""), server.c_str());
    return;
  }

  std::vector<Quark>& groups (s->groups.get_container());

  AlphabeticalQuarkOrdering o;
  StringView line, name, numbers;
  bool needs_sort (false);
  Quark prev;
  std::vector<Quark> tmp_sub, tmp_unsub;
  tmp_sub.reserve (1000);
  tmp_unsub.reserve (120000); // giganews has ~100k; anyone have more?
  while (!in->fail() && in->getline (line))
  {
    bool subscribed;
    if (parse_newsrc_line (line, name, subscribed, numbers))
    {
      Quark const &group(name);

      needs_sort |= (!prev.empty() && !o(prev,group));
      groups.push_back (group);

      if (subscribed)
        tmp_sub.push_back (group);
      else
        tmp_unsub.push_back (group);

      if (!numbers.empty())
        _read_groups[group][server]._read.mark_str (numbers);

      prev = group;
    }
  }

  // sub += tmp_sub
  if (needs_sort)
    std::sort (tmp_sub.begin(), tmp_sub.end(), AlphabeticalQuarkOrdering());
  if (sub.empty())
    sub.get_container().swap (tmp_sub);
  else {
    std::vector<Quark> tmp;
    tmp.reserve (sub.size() + tmp_sub.size());
    std::set_union (sub.begin(), sub.end(),
                    tmp_sub.begin(), tmp_sub.end(),
                    std::inserter (tmp, tmp.begin()), o);
    sub.get_container().swap (tmp);
  }

  // unsub += tmp_unsub
  if (needs_sort)
    std::sort (tmp_unsub.begin(), tmp_unsub.end(), AlphabeticalQuarkOrdering());
  if (unsub.empty())
    unsub.get_container().swap (tmp_unsub);
  else {
    std::vector<Quark> tmp;
    tmp.reserve (unsub.size() + tmp_unsub.size());
    std::set_union (unsub.begin(), unsub.end(),
                    tmp_unsub.begin(), tmp_unsub.end(),
                    std::inserter (tmp, tmp.begin()), o);
    unsub.get_container().swap (tmp);
  }
}

void DataImpl ::load_newsrc_files(DataIO const &data_io)
{
  alpha_groups_t& s(_subscribed);
  alpha_groups_t& u(_unsubscribed);

  s.clear ();
  u.clear ();

  foreach_const (servers_t, _servers, sit) {
    const Quark key (sit->first);
    const std::string filename = file::absolute_fn("", sit->second.newsrc_filename);
    pan_debug("reading " << sit->second.host << " groups from " << filename);
    if (file::file_exists (filename.c_str())) {
      LineReader * in (data_io.read_file (filename));
      load_newsrc (key, in, s, u);
      delete in;
    }
  }

  // remove duplicates
  s.erase (std::unique(s.begin(), s.end()), s.end());
  u.erase (std::unique(u.begin(), u.end()), u.end());

  // unsub -= sub
  AlphabeticalQuarkOrdering o;
  std::vector<Quark> tmp;
  tmp.reserve (u.size());
  std::set_difference (u.begin(), u.end(), s.begin(), s.end(), inserter (tmp, tmp.begin()), o);
  u.get_container().swap (tmp);

  // shrink-to-fit
  alpha_groups_t (s).swap(s);
  alpha_groups_t (u).swap(u);
  fire_grouplist_rebuilt ();
}

void
DataImpl :: save_newsrc_files (DataIO& data_io) const
{
  if (newsrc_autosave_id) {
    g_source_remove( newsrc_autosave_id );
    newsrc_autosave_id = 0;
  }

  if (_unit_test)
    return;

  // overly-complex optimization: both sit->second.groups and _subscribed
  // are both ordered by AlphabeticalQuarkOrdering.
  // Where N==sit->second.groups.size() and M==_subscribed.size(),
  // "bool subscribed = _subscribed.count (group)" is N*log(M),
  // but a sorted set comparison is M+N comparisons.
  AlphabeticalQuarkOrdering o;

  // save all the servers' newsrc files
  foreach_const (servers_t, _servers, sit)
  {
    const Quark& server (sit->first);

    // write this server's newsrc
    const std::string filename = file::absolute_fn("", sit->second.newsrc_filename);

    std::ostream& out (*data_io.write_file (filename));
    std::string newsrc_string;
    alpha_groups_t::const_iterator sub_it (_subscribed.begin());
    const alpha_groups_t::const_iterator sub_end(_subscribed.end());
    foreach_const (Server::groups_t, sit->second.groups, git) // for the groups in this server...
    {
      const Quark& group (*git);

      //const bool subscribed (_subscribed.count (group));
      while (sub_it!=sub_end && o (*sub_it, group)) ++sub_it; // see comment for 'o' above
      const bool subscribed (sub_it!=sub_end && *sub_it==group);
      out << group;
      out.put (subscribed ? ':' : '!');

      // if the group's been read, save its read number ranges...
      const ReadGroup::Server * rgs (find_read_group_server (group, server));
      if (rgs != nullptr) {
        newsrc_string.clear ();
        rgs->_read.to_string (newsrc_string);
        if (!newsrc_string.empty()) {
          out.put (' ');
          out << newsrc_string;
        }
      }

      out.put ('\n');
    }
    data_io.write_done (&out);
  }
}

/***
****
***/

void
DataImpl :: load_group_permissions (const DataIO& data_io)
{
  std::vector<Quark> m, n;

  LineReader * in (data_io.read_group_permissions ());
  StringView s, line;
  while (in && !in->fail() && in->getline(line))
  {
    if (line.len && *line.str=='#')
      continue;

    else if (!line.pop_last_token (s, ':') || !s.len || (*s.str!='y' && *s.str!='n' && *s.str!='m')) {
      std::cerr << LINE_ID << " Group permissions: Can't parse line `" << line << std::endl;
      continue;
    }

    const Quark group (line);
    const char ch = *s.str;

    if (ch == 'm')
      m.push_back (group);
    else if (ch == 'n')
      n.push_back (group);
  }

  std::sort (m.begin(), m.end());
  m.erase (std::unique(m.begin(), m.end()), m.end());
  _moderated.get_container().swap (m);

  std::sort (n.begin(), n.end());
  n.erase (std::unique(n.begin(), n.end()), n.end());
  _nopost.get_container().swap (n);

  delete in;
}

void
DataImpl :: save_group_permissions (DataIO& data_io) const
{
  if (_unit_test)
    return;

  std::ostream& out (*data_io.write_group_permissions ());

  typedef std::map<Quark, char, AlphabeticalQuarkOrdering> tmp_t;
  tmp_t tmp;
  foreach_const (groups_t, _moderated, it) tmp[*it] = 'm';
  foreach_const (groups_t, _nopost, it) tmp[*it] = 'n';

  out << "# Permissions: y means posting ok; n means posting not okay; m means moderated.\n"
      << "# Since almost all groups allow posting, Pan assumes that as the default.\n"
      << "# Only moderated or no-posting groups are listed here.\n";
  foreach_const (tmp_t, tmp, it) {
    out << it->first;
    out.put (':');
    out << it->second;
    out.put ('\n');
  }
  data_io.write_done (&out);
}

/***
****
***/

void
DataImpl :: ensure_descriptions_are_loaded () const
{
  if (!_descriptions_loaded)
  {
    _descriptions_loaded = true;
    load_group_descriptions (*_data_io);
  }
}

void
DataImpl :: load_group_descriptions (const DataIO& data_io) const
{
  _descriptions.clear ();

  LineReader * in (data_io.read_group_descriptions ());
  StringView s, group;
  while (in && !in->fail() && in->getline(group))
    if (group.pop_last_token (s, ':'))
      _descriptions[group].assign (s.str, s.len);
  delete in;
}

void
DataImpl :: load_group_xovers (const DataIO& data_io)
{
  LineReader * in (data_io.read_group_xovers ());
  if (in && !in->fail())
  {
    StringView line;
    StringView groupname, total, unread;
    StringView xover, servername, low;

    // walk through the groups line-by-line...
    while (in->getline (line))
    {
      // skip comments and blank lines
      line.trim();
      if (line.empty() || *line.str=='#')
        continue;

      if (line.pop_token(groupname) && line.pop_token(total) && line.pop_token(unread))
      {
        ReadGroup& g (_read_groups[groupname]);
        g._article_count = Article_Count(total);
        g._unread_count = Article_Count(unread);

        while (line.pop_token (xover))
          if (xover.pop_token(servername,':'))
            g[servername]._xover_high = Article_Number(xover);
      }
    }
  }
  delete in;
}

void
DataImpl :: save_group_descriptions (DataIO& data_io) const
{
  if (_unit_test)
    return;

  assert (_descriptions_loaded);

  typedef std::map<Quark, std::string, AlphabeticalQuarkOrdering> tmp_t;
  tmp_t tmp;
  foreach_const (descriptions_t, _descriptions, it)
    tmp[it->first] = it->second;

  std::ostream& out (*data_io.write_group_descriptions ());
  foreach_const (tmp_t, tmp, it) {
    out << it->first;
    out.put (':');
    out << it->second;
    out.put ('\n');
  }
  data_io.write_done (&out);
}

namespace
{
  typedef std::map < pan::Quark, std::string > quark_to_symbol_t;

  struct QuarkToSymbol {
    const quark_to_symbol_t& _map;
    virtual ~QuarkToSymbol () {}
    QuarkToSymbol (const quark_to_symbol_t& map): _map(map) { }
    virtual std::string operator() (const Quark& quark) const {
      quark_to_symbol_t::const_iterator it (_map.find (quark));
      return it!=_map.end() ? it->second : quark.to_string();
    }
  };
}

void
DataImpl :: save_group_xovers (DataIO& data_io) const
{
  if (_unit_test)
    return;

  std::ostream& out (*data_io.write_group_xovers ());

  // find the set of groups that have had an xover
  typedef std::set<Quark, AlphabeticalQuarkOrdering> xgroups_t;
  xgroups_t xgroups;
  foreach_const (read_groups_t, _read_groups, git) {
    const ReadGroup& group (git->second);
    bool is_xgroup (static_cast<uint64_t>(group._article_count) != 0 || static_cast<uint64_t>(group._unread_count) != 0);
    if (!is_xgroup)
      foreach_const (ReadGroup::servers_t, group._servers, sit)
        if ((is_xgroup = (static_cast<uint64_t>(sit->second._xover_high)!=0)))
          break;
    if (is_xgroup)
      xgroups.insert (git->first);
  }

  out << "# groupname totalArticleCount unreadArticleCount [server:latestXoverHigh]*\n";

  // foreach xgroup
  foreach_const (xgroups_t, xgroups, it)
  {
    const Quark groupname (*it);
    const ReadGroup& g (*find_read_group (groupname));
    out << groupname;
    out.put (' ');
    out << g._article_count;
    out.put (' ');
    out << g._unread_count;
    foreach_const (ReadGroup::servers_t, g._servers, i) {
      if (static_cast<uint64_t>(i->second._xover_high) != 0) {
        out.put (' ');
        out << i->first;
        out.put (':');
        out << i->second._xover_high;
      }
    }
    out.put ('\n');
  }

  data_io.write_done (&out);
}

/****
*****
****/

Article_Number
DataImpl :: get_xover_high (const Quark  & groupname,
                            const Quark  & servername) const
{
  Article_Number high (0ul);
  const ReadGroup::Server * rgs (find_read_group_server (groupname, servername));
  if (rgs)
    high = rgs->_xover_high;
  return high;
}

void
DataImpl :: set_xover_high (const Quark & group,
                            const Quark & server,
                            const Article_Number high)
{
  //std::cerr << LINE_ID << "setting " << get_server_address(server) << ':' << group << " xover high to " << high << std::endl;
  ReadGroup::Server& rgs (_read_groups[group][server]);
  rgs._xover_high = high;
}

void
DataImpl :: add_groups (const Quark       & server,
                        const NewGroup    * newgroups,
                        size_t              count)
{
  ensure_descriptions_are_loaded ();

  Server * s (find_server (server));
  assert (s != nullptr);

  {
    AlphabeticalQuarkOrdering o;
    Server::groups_t groups;
    std::vector<Quark> tmp;

    // make a groups_t from the added groups,
    // and merge it with the server's list of groups
    groups.get_container().reserve (count);
    for (const NewGroup *it=newgroups, *end=newgroups+count; it!=end; ++it)
      groups.get_container().push_back (it->group);
    groups.sort ();
    std::set_union (s->groups.begin(), s->groups.end(),
                    groups.begin(), groups.end(),
                    std::back_inserter (tmp), o);
    tmp.erase (std::unique(tmp.begin(), tmp.end()), tmp.end());
    s->groups.get_container().swap (tmp);

    // make a groups_t of groups we didn't already have,
    // and merge it with _unsubscribed (i.e., groups we haven't seen before become unsubscribed)
    groups.clear ();
    for (const NewGroup *it=newgroups, *end=newgroups+count; it!=end; ++it)
      if (!_subscribed.count (it->group))
        groups.get_container().push_back (it->group);
    groups.sort ();
    tmp.clear ();
    std::set_union (groups.begin(), groups.end(),
                    _unsubscribed.begin(), _unsubscribed.end(),
                    std::back_inserter (tmp), o);
    tmp.erase (std::unique(tmp.begin(), tmp.end()), tmp.end());
    _unsubscribed.get_container().swap (tmp);
  }

  {
    // build lists of the groups that should and should not be in _moderated and _nopost.t
    // this is pretty cumbersome, but since these lists almost never change it's still
    // a worthwhile tradeoff to get the speed/memory wins of a sorted_vector
    groups_t mod, notmod, post, nopost, tmp;
    for (const NewGroup *it=newgroups, *end=newgroups+count; it!=end; ++it) {
      if (it->permission == 'm') mod.get_container().push_back (it->group);
      if (it->permission != 'm') notmod.get_container().push_back (it->group);
      if (it->permission == 'n') nopost.get_container().push_back (it->group);
      if (it->permission != 'n') post.get_container().push_back (it->group);
    }
    mod.sort (); notmod.sort ();
    post.sort (); nopost.sort ();

    // _moderated -= notmod
    tmp.clear ();
    std::set_difference (_moderated.begin(), _moderated.end(), notmod.begin(), notmod.end(), inserter (tmp, tmp.begin()));
    _moderated.swap (tmp);
    // _moderated += mod
    tmp.clear ();
    std::set_union (_moderated.begin(), _moderated.end(), mod.begin(), mod.end(), inserter (tmp, tmp.begin()));
    _moderated.swap (tmp);

    // _nopost -= post
    tmp.clear ();
    std::set_difference (_nopost.begin(), _nopost.end(), post.begin(), post.end(), inserter (tmp, tmp.begin()));
    _nopost.swap (tmp);
    // _nopost += nopost
    tmp.clear ();
    std::set_union (_nopost.begin(), _nopost.end(), nopost.begin(), nopost.end(), inserter (tmp, tmp.begin()));
    _nopost.swap (tmp);
  }

  // keep any descriptions worth keeping that we don't already have...
  for (const NewGroup *it=newgroups, *end=newgroups+count; it!=end; ++it) {
    const NewGroup& ng (*it);
    if (!ng.description.empty() && ng.description!="?")
      _descriptions[ng.group] = ng.description;
  }

  save_group_descriptions (*_data_io);
  save_group_permissions (*_data_io);
  fire_grouplist_rebuilt ();
}

void
DataImpl :: mark_group_read (const Quark& groupname)
{
  ReadGroup * rg (find_read_group (groupname));
  if (rg != nullptr) {
    foreach (ReadGroup::servers_t, rg->_servers, it) {
      //std::cerr << LINE_ID << " marking read range [0..." << it->second._xover_high << "] in " << get_server_address(it->first) << ']' << std::endl;
      it->second._read.mark_range (static_cast<Article_Number>(0), it->second._xover_high, true);
    }
    rg->_unread_count = static_cast<Article_Count>(0);
    save_group_xovers (*_data_io);
    fire_group_read (groupname);
  }
}

void
DataImpl :: set_group_subscribed (const Quark& group, bool subscribed)
{
  if (subscribed) {
    _unsubscribed.erase (group);
    _subscribed.insert (group);
  } else {
    _subscribed.erase (group);
    _unsubscribed.insert (group);
  }

  fire_group_subscribe (group, subscribed);
}

const std::string&
DataImpl :: get_group_description (const Quark& group) const
{
  ensure_descriptions_are_loaded ();
  static const std::string nil;
  descriptions_t::const_iterator it (_descriptions.find (group));
  return it == _descriptions.end() ? nil : it->second;
}

void
DataImpl :: get_group_counts (const Quark   & groupname,
                              Article_Count & unread_count,
                              Article_Count & article_count) const
{
  const ReadGroup * g (find_read_group (groupname));
  if (!g)
    unread_count = article_count = static_cast<Article_Count>(0ul);
  else {
    unread_count = g->_unread_count;
    article_count = g->_article_count;
  }
}

char
DataImpl :: get_group_permission (const Quark & group) const
{
  if (_moderated.count (group))
    return 'm';
  else if (_nopost.count (group))
    return 'n';
  else
    return 'y';
}


void
DataImpl :: group_get_servers (const Quark& groupname, quarks_t& addme) const
{
  foreach_const (servers_t, _servers, it)
    if (it->second.groups.count (groupname))
      addme.insert (it->first);
}

void
DataImpl :: server_get_groups (const Quark& servername, quarks_t& addme) const
{
  const Server * server (find_server (servername));
  if (server)
    addme.insert (server->groups.begin(), server->groups.end());
}

void
DataImpl :: get_subscribed_groups (std::vector<Quark>& setme) const
{
  setme.assign (_subscribed.begin(), _subscribed.end());
}

void
DataImpl :: get_other_groups (std::vector<Quark>& setme) const
{
  setme.assign (_unsubscribed.begin(), _unsubscribed.end());
}
