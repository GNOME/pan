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

#include <cerrno>
#include <cmath>
#include <config.h>
#include <fstream>
#include <glib.h>
#include <glib/gi18n.h>
#include <map>
#include <string>
#include <vector>
extern "C"
{
#include <sys/stat.h>  // for chmod
#include <sys/types.h> // for chmod
}
#include "article-filter.h"
#include "data-impl.h"
#include <pan/data/article.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/quark.h>
#include <pan/general/time-elapsed.h>
#include <pan/usenet-utils/filter-info.h>

using namespace pan;

DataImpl ::GroupHeaders ::GroupHeaders() :
  _ref(0),
  _dirty(false)
{
}

DataImpl ::GroupHeaders ::~GroupHeaders()
{
}

DataImpl ::ArticleNode *DataImpl ::GroupHeaders ::find_node(Quark const &mid)
{
  ArticleNode *node(nullptr);
  nodes_t::iterator it(_nodes.find(mid));
  if (it != _nodes.end())
  {
    node = it->second;
  }
  return node;
}

DataImpl ::ArticleNode const *DataImpl ::GroupHeaders ::find_node(
  Quark const &mid) const
{
  ArticleNode const *node(nullptr);
  nodes_t::const_iterator it(_nodes.find(mid));
  if (it != _nodes.end())
  {
    node = it->second;
  }
  return node;
}

Quark const &DataImpl ::GroupHeaders ::find_parent_message_id(
  Quark const &mid) const
{
  ArticleNode const *node(find_node(mid));
  if (node && node->_parent)
  {
    return node->_parent->_mid;
  }

  static const Quark empty_quark;
  return empty_quark;
}

Article const *DataImpl ::GroupHeaders ::find_article(
  Quark const &message_id) const
{
  Article *a(nullptr);

  ArticleNode const *node(find_node(message_id));
  if (node)
  {
    a = node->_article;
  }

  return a;
}

Article *DataImpl ::GroupHeaders ::find_article(Quark const &message_id)
{
  Article *a(nullptr);

  ArticleNode const *node(find_node(message_id));
  if (node)
  {
    a = node->_article;
  }

  return a;
}

void DataImpl ::GroupHeaders ::remove_articles(quarks_t const &mids)
{
  nodes_v nodes;
  find_nodes(mids, _nodes, nodes);
  foreach (nodes_v, nodes, it)
  {
    (*it)->_article = nullptr;
  }
  _dirty = true;
}

DataImpl ::GroupHeaders const *DataImpl ::get_group_headers(
  Quark const &group) const
{
  group_to_headers_t::const_iterator it(_group_to_headers.find(group));
  return it == _group_to_headers.end() ? nullptr : it->second;
}

DataImpl ::GroupHeaders *DataImpl ::get_group_headers(Quark const &group)
{
  group_to_headers_t::iterator it(_group_to_headers.find(group));
  return it == _group_to_headers.end() ? nullptr : it->second;
}

void DataImpl ::GroupHeaders ::build_references_header(Article const *article,
                                                       std::string &setme) const
{
  setme.clear();
  Quark const &message_id(article->message_id);
  ArticleNode const *node(find_node(message_id));
  while (node && node->_parent)
  {
    node = node->_parent;
    StringView const &ancestor_mid = node->_mid.to_view();
    setme.insert(0, ancestor_mid.str, ancestor_mid.len);
    if (node->_parent)
    {
      setme.insert(0, 1, ' ');
    }
  }
}

void DataImpl::GroupHeaders::reserve(Article_Count articles)
{
  _art_chunk.reserve(static_cast<Article_Count::type>(articles));
  // A note - the number of nodes is very rarely the same as the number of
  // articles, but it is generally not far out, so at worse you'll end up with
  // two allocations
  _node_chunk.reserve(static_cast<Article_Count::type>(articles));
}

void DataImpl ::get_article_references(Quark const &group,
                                       Article const *article,
                                       std::string &setme) const
{
  GroupHeaders const *h(get_group_headers(group));
  if (! h)
  {
    setme.clear();
  }
  else
  {
    h->build_references_header(article, setme);
  }
}

void DataImpl ::free_group_headers_memory(Quark const &group)
{
  group_to_headers_t::iterator it(_group_to_headers.find(group));
  if (it != _group_to_headers.end())
  {
    delete it->second;
    _group_to_headers.erase(it);
  }
}

void DataImpl ::ref_group(Quark const &group)
{
  GroupHeaders *h(get_group_headers(group));
  if (! h)
  {
    h = _group_to_headers[group] = new GroupHeaders();
    load_headers(*_data_io, group);
  }
  ++h->_ref;
  //  std::cerr << LINE_ID << " group " << group << " refcount up to " <<
  //  h->_ref << std::endl;
}

void DataImpl ::unref_group(Quark const &group)
{
  GroupHeaders *h(get_group_headers(group));
  pan_return_if_fail(h != nullptr);

  --h->_ref;
  //  std::cerr << LINE_ID << " group " << group << " refcount down to " <<
  //  h->_ref << std::endl;
  if (h->_ref == 0)
  {
    //    if (h->_dirty )
    save_headers(*_data_io, group);
    h->_dirty = false;
    free_group_headers_memory(group);
  }
}

void DataImpl ::fire_article_flag_changed(articles_t &a, Quark const &group)
{
  GroupHeaders *h(get_group_headers(group));
  h->_dirty = true;
  Data::fire_article_flag_changed(a, group);
}

void DataImpl ::find_nodes(quarks_t const &mids, nodes_t &nodes, nodes_v &setme)
{
  NodeWeakOrdering o;
  nodes_t tmp;
  std::set_intersection(nodes.begin(),
                        nodes.end(),
                        mids.begin(),
                        mids.end(),
                        std::inserter(tmp, tmp.begin()),
                        o);

  setme.reserve(tmp.size());
  foreach_const (nodes_t, tmp, it)
  {
    setme.push_back(it->second);
  }
}

void DataImpl ::find_nodes(quarks_t const &mids,
                           nodes_t const &nodes,
                           const_nodes_v &setme)
{
  NodeWeakOrdering o;
  nodes_t tmp;
  std::set_intersection(nodes.begin(),
                        nodes.end(),
                        mids.begin(),
                        mids.end(),
                        std::inserter(tmp, tmp.begin()),
                        o);

  setme.reserve(tmp.size());
  foreach_const (nodes_t, tmp, it)
  {
    setme.push_back(it->second);
  }
}

/*******
********
*******/

// 'article' must have been instantiated by
// GroupHeaders::alloc_new_article()!!
void DataImpl ::load_article(Quark const &group,
                             Article *article,
                             StringView const &references)

{
#if 0
  std::cerr << LINE_ID << " adding article "
            << " subject [" << article->subject << ']'
            << " mid [" << article->message_id <<  ']'
            << " references [" << references << ']'
            << std::endl;
#endif

  GroupHeaders *h(get_group_headers(group));
  pan_return_if_fail(h != nullptr);

  // populate the current node
  Quark const &mid(article->message_id);
  ArticleNode *node(h->_nodes[mid]);
  if (! node)
  {
    static const ArticleNode blank_node;
    h->_node_chunk.push_back(blank_node);
    node = h->_nodes[mid] = &h->_node_chunk.back();
    node->_mid = mid;
  }
  // !!INFO!! : this is bypassed for now, as it causes an abort on local cache
  // corruptions
  // assert (!node->_article);
  node->_article = article;
  ArticleNode *article_node = node;

  // build nodes for each of the references
  StringView tok, refs(references);
  // std::cerr << LINE_ID << " references [" << refs << ']' << std::endl;
  while (refs.pop_last_token(tok, ' '))
  {
    tok.trim();
    if (tok.empty())
    {
      break;
    }

    ArticleNode *old_parent_node(node->_parent);
    const Quark old_parent_mid(old_parent_node ? old_parent_node->_mid :
                                                 Quark());
    const Quark new_parent_mid(tok);
    // std::cerr << LINE_ID << " now we're working on " << new_parent_mid <<
    // std::endl;

    if (new_parent_mid == old_parent_mid)
    {
      // std::cerr << LINE_ID << " our tree agrees with the References header
      // here..." << std::endl;
      node = node->_parent;
      continue;
    }

    if (! old_parent_node)
    {
      // std::cerr << LINE_ID << " haven't mapped " << new_parent_mid << "
      // before..." << std::endl;
      ArticleNode *new_parent_node(h->_nodes[new_parent_mid]);
      bool const found(new_parent_node != nullptr);
      if (! found)
      {
        // std::cerr << LINE_ID << " didn't find it; adding new node for " <<
        // new_parent_mid << std::endl;
        static const ArticleNode blank_node;
        h->_node_chunk.push_back(blank_node);
        new_parent_node = h->_nodes[new_parent_mid] = &h->_node_chunk.back();
        new_parent_node->_mid = new_parent_mid;
      }
      node->_parent = new_parent_node;
      if (find_ancestor(new_parent_node, new_parent_mid))
      {
        node->_parent = nullptr;
        // std::cerr << LINE_ID << " someone's been munging References headers
        // to cause trouble!" << std::endl;
        break;
      }
      new_parent_node->_children.push_front(node);
      node = new_parent_node;
      continue;
    }

    ArticleNode *tmp;
    if ((tmp = find_ancestor(node, new_parent_mid)))
    {
      // std::cerr << LINE_ID << " this References header has a hole... jumping
      // to " << tmp->_mid << std::endl;
      node = tmp;
      continue;
    }

    char const *cpch;
    if ((cpch = refs.strstr(old_parent_mid.to_view())))
    {
      // std::cerr << LINE_ID << " this References header fills a hole of ours
      // ... " << new_parent_mid << std::endl;

      // unlink from old parent
      old_parent_node->_children.remove(node);
      node->_parent = nullptr;

      // link to new parent
      ArticleNode *new_parent_node(h->_nodes[new_parent_mid]);
      bool const found(new_parent_node != nullptr);
      if (! found)
      {
        // std::cerr << LINE_ID << " didn't find it; adding new node for " <<
        // new_parent_mid << std::endl;
        static const ArticleNode blank_node;
        h->_node_chunk.push_back(blank_node);
        new_parent_node = h->_nodes[new_parent_mid] = &h->_node_chunk.back();
        new_parent_node->_mid = new_parent_mid;
      }
      node->_parent = new_parent_node;
      if (find_ancestor(new_parent_node, new_parent_mid) != nullptr)
      {
        node->_parent = nullptr;
        // std::cerr << LINE_ID << " someone's been munging References headers
        // to cause trouble!" << std::endl;
        break;
      }
      new_parent_node->_children.push_front(node);
      node = new_parent_node;
      continue;
    }
  }

  // recursion?
  assert(find_ancestor(article_node, article->message_id) == nullptr);
}

#if 0
std::string
DataImpl :: get_references (const Quark& group, const Article& a) const
{
  std::string s;

  GroupHeaders * h (get_group_headers (group));
  pan_return_if_fail (h!=0);

  const Quark& mid (a.message_id);
  ArticleNode * node (h->_nodes[mid]);
  node = node->parent;
  while (node) {
    if (!s.empty())
      s.insert (0, 1, ' ');
    const StringView v (node->_mid.to_view());
    s.insert (0, v.str, v.len);
    node = node->parent;
  }
//  std::cerr << "article " << a.message_id << " references " << s << std::endl;
  return s;
}
#endif

void DataImpl ::load_part(Quark const &group,
                          Quark const &mid,
                          int number,
                          StringView const &part_mid,
                          unsigned long lines,
                          unsigned long bytes)
{
  GroupHeaders *h = get_group_headers(group);
  Article *a(h->find_article(mid));
  pan_return_if_fail(a != nullptr);

  if (a->add_part(number, part_mid, bytes))
  {
    a->lines += lines;
  }
}

namespace {
unsigned long long view_to_ull(StringView const &view)
{
  unsigned long long val(0ull);
  if (! view.empty())
  {
    errno = 0;
    val = strtoull(view.str, nullptr, 10);
    if (errno)
    {
      val = 0ull;
    }
  }
  return val;
}
} // namespace

// load headers from internal file in ~/.pan2/groups
void DataImpl ::load_headers(DataIO const &data_io, Quark const &group)
{
  TimeElapsed timer;

  GroupHeaders *h(get_group_headers(group));
  assert(h != nullptr);

  Article_Count article_count(0);
  Article_Count unread_count(0);
  StringView line;
  bool success(false);
  quarks_t servers;

  ArticleFilter::sections_t score_sections;
  _scorefile.get_matching_sections(StringView(group), score_sections);

  char const *groupname(group.c_str());
  LineReader *in(data_io.read_group_headers(group));
  if (in && ! in->fail())
  {
    do
    { // skip past the comments at the top
      in->getline(line);
      line.trim();
    } while (! line.empty() && *line.str == '#');

    int const version(atoi(line.str));
    if (version == 1 || version == 2 || version == 3)
    {
      // build the symbolic server / group lookup table
      in->getline(line);
      int symbol_qty(atoi(line.str));
      Quark xref_lookup[CHAR_MAX];
      for (int i = 0; i < symbol_qty; ++i)
      {
        StringView key;
        in->getline(line);
        line.trim();
        if (line.pop_token(key, '\t') && key.len == 1)
        {
          xref_lookup[(int)*key.str] = line;
        }
      }

      // build the author lookup table
      in->getline(line);
      symbol_qty = atoi(line.str);
      Quark author_lookup[CHAR_MAX];
      for (int i = 0; i < symbol_qty; ++i)
      {
        StringView key;
        in->getline(line);
        line.trim();
        if (line.pop_token(key, '\t') && key.len == 1)
        {
          author_lookup[(int)*key.str] = line;
        }
      }

      Xref::targets_t targets;
      std::vector<Xref::Target> &targets_v(targets.get_container());

      // each article in this group...
      unsigned int expire_count(0);
      in->getline(line);
      Article_Count const article_qty{line};
      h->reserve(article_qty);

      const time_t now(time(nullptr));
      PartBatch part_batch;
      for (;;)
      {
        // look for the beginning of an Article record.
        // it'll be a message-id line with no leading whitespace.
        StringView s;
        if (! in->getline(s)) // end of file
        {
          break;
        }

        Article &a(h->alloc_new_article());

        // flag line
        a.flag = false;
        if (version == 3)
        {
          a.flag = atoi(s.str) == 1 ? true : false;
          in->getline(s);
        }

        if (s.empty() || *s.str != '<') // not a message-id...
        {
          continue;
        }

        // message id
        s.ltrim();
        a.message_id = s;

        // subject line

        in->getline(s);
        s.ltrim();
        a.subject = s;

        // author line
        in->getline(s);
        s.ltrim();
        a.author = s.len == 1 ? author_lookup[(int)*s.str] : Quark(s);

        // optional references line
        std::string references;
        in->getline(s);
        s.ltrim();
        if (! s.empty() && *s.str == '<')
        {
          references = s;
          in->getline(s);
          s.ltrim();
        }

        // date-posted line
        a.time_posted = view_to_ull(s);
        int const days_old((now - a.time_posted) / (24 * 60 * 60));

        // xref line
        in->getline(s);
        s.ltrim();
        const size_t max_targets(std::count(s.begin(), s.end(), ' ') + 1);
        targets_v.resize(max_targets);
        Xref::Target *target_it(&targets_v.front());
        StringView tok, server_tok, group_tok;
        while (s.pop_token(tok))
        {
          if (tok.pop_token(server_tok, ':') && tok.pop_token(group_tok, ':'))
          {
            target_it->server = server_tok;
            target_it->group = group_tok.len == 1 ?
                                 xref_lookup[(int)*group_tok.str] :
                                 Quark(group_tok);
            target_it->number = Article_Number(tok);
            Server const *server(find_server(target_it->server));
            if (server
                && ((! server->article_expiration_age)
                    || (days_old <= server->article_expiration_age)))
            {
              ++target_it;
            }
          }
        }
        targets_v.resize(target_it - &targets_v.front());
        targets.sort();
        bool expired(targets.empty());
        a.xref.swap(targets);

        // is_binary [total_part_count found_part_count]
        int total_part_count(1);
        int found_part_count(1);
        in->getline(s);
        s.ltrim();
        s.pop_token(tok);
        a.is_binary = ! tok.empty() && tok.str[0] == 't';
        if (a.is_binary)
        {
          s.ltrim();
          s.pop_token(tok);
          total_part_count = atoi(tok.str);
          s.ltrim();
          s.pop_token(tok);
          found_part_count = atoi(tok.str);
        }
        s.ltrim();
        if (s.pop_token(tok))
        {
          a.lines = atoi(tok.str); // this field was added in 0.115
        }

        // found parts...
        part_batch.init(a.message_id, total_part_count);
        //        std::cerr<<"article "<<a.message_id<<" "<<total_part_count<<"
        //        "<<found_part_count<<std::endl;
        for (int i(0), count(found_part_count); i < count; ++i)
        {
          bool const gotline(in->getline(s));

          if (gotline && ! expired)
          {
            StringView tok;
            s.ltrim();
            s.pop_token(tok);
            int const number(atoi(tok.str));
            if (number > total_part_count)
            { // corrupted entry
              expired = true;
              break;
            }
            StringView part_mid;
            unsigned long part_bytes(0);
            s.ltrim();
            s.pop_token(part_mid);
            if (part_mid.len == 1 && *part_mid.str == '"')
            {
              part_mid = a.message_id.to_view();
            }
            s.pop_token(tok);
            part_bytes = view_to_ull(tok);
            part_batch.add_part(number, part_mid, part_bytes);

            if (s.pop_token(tok))
            {
              a.lines += atoi(tok.str); // this field was removed in 0.115
            }
          }
        }
        if (! expired)
        {
          a.set_parts(part_batch);
        }

        // add the article to the group if it hasn't all expired
        if (expired)
        {
          ++expire_count;
        }
        else
        {
          load_article(group, &a, references);
          a.score = _article_filter.score_article(
            *this,
            score_sections,
            group,
            a); // score _after_ threading, so References: works
          ++article_count;
          if (! is_read(&a))
          {
            ++unread_count;
          }
        }
      }

      if (expire_count)
      {
        Log::add_info_va(_("Expired %lu old articles from \"%s\""),
                         expire_count,
                         group.c_str());
      }

      success = ! in->fail();
    }
    else
    {
      Log::add_urgent_va(_("Unsupported data version for %s headers: %d.\nAre "
                           "you running an old version of Pan by accident?"),
                         groupname,
                         version);
    }
  }
  delete in;

  // update the group's article count...
  ReadGroup &g(_read_groups[group]);
  g._unread_count = unread_count;
  g._article_count = article_count;
  fire_group_counts(group, unread_count, article_count);

  if (success)
  {
    double const seconds = timer.get_seconds_elapsed();
    Log::add_info_va(
      _("Loaded %llu articles for \"%s\" in %.1f seconds (%.0f per second)"),
      static_cast<uint64_t>(article_count),
      group.c_str(),
      seconds,
      static_cast<uint64_t>(article_count)
        / (fabs(seconds) < 0.001 ? 0.001 : seconds));
  }
}

namespace {
char const *lookup_symbols("abcdefghijklmnopqrstuvwxyz"
                           "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                           "1234567890!@#$%^&*()");

struct QuarkToSymbol
{
    char buf[2];

    QuarkToSymbol()
    {
      buf[1] = '\0';
    }

    typedef Loki::AssocVector<pan::Quark, char> quark_to_symbol_t;
    quark_to_symbol_t _map;

    char const *operator()(Quark const &quark)
    {
      quark_to_symbol_t::const_iterator it(_map.find(quark));
      if (it == _map.end())
      {
        return quark.c_str();
      }

      buf[0] = it->second;
      return buf;
    }

    void write(std::ostream &out, StringView const &comment) const
    {
      Quark quarks[UCHAR_MAX];
      foreach_const (quark_to_symbol_t, _map, it)
      {
        quarks[(int)it->second] = it->first;
      }

      const size_t len(_map.size());
      out << len;
      if (! comment.empty())
      {
        out << "\t # " << comment << '\n';
      }
      for (size_t i(0); i != len; ++i)
      {
        char const ch(lookup_symbols[i]);
        out << '\t' << ch << '\t' << quarks[(int)ch] << '\n';
      }
    }
};

typedef Loki::AssocVector<Quark, unsigned long> frequency_t;

void build_qts(frequency_t &freq, QuarkToSymbol &setme)
{
  setme._map.clear();

  typedef std::multimap<unsigned long, Quark> counts_t;
  counts_t counts;
  foreach_const (frequency_t, freq, it)
  {
    counts.insert(std::pair<unsigned long, Quark>(it->second, it->first));
  }

  counts_t::const_reverse_iterator it = counts.rbegin(), end = counts.rend();
  for (char const *pch = lookup_symbols; *pch && it != end; ++pch, ++it)
  {
    setme._map[it->second] = *pch;
  }
  freq.clear();
}
} // namespace

bool DataImpl ::save_headers(DataIO &data_io,
                             Quark const &group,
                             std::vector<Article *> const &articles,
                             unsigned long &part_count,
                             unsigned long &article_count) const
{
  char const endl('\n');
  GroupHeaders const *h(get_group_headers(group));
  assert(h != nullptr);

  part_count = 0;
  article_count = 0;

  bool success;
  if (_unit_test)
  {
    Log::add_info_va("Not saving %s's headers because we're in unit test mode",
                     group.c_str());
    success = true;
  }
  else
  {
    std::ostream &out(*data_io.write_group_headers(group));

    out << "#\n"
           "# This file has three sections.\n"
           "#\n"
           "# A. A shorthand table for the most frequent groups in the xrefs.\n"
           "#    The first line tells the number of elements to follow,\n"
           "#    then one line per entry with a one-character shorthand and "
           "full name.\n"
           "#\n"
           "# B. A shorthand table for the most freqent author names.\n"
           "#    This is formatted just like the other shorthand table.\n"
           "#    (sorted by post count, so it's also a most-frequent-posters "
           "list...)\n"
           "#\n"
           "# C. The group's headers section.\n"
           "#    The first line tells the number of articles to follow,\n"
           "#    then articles which each have the following lines:\n"
           "#    1. message-id\n"
           "#    2. subject\n"
           "#    3. author\n"
           "#    4. references. This line is omitted if the Article has an "
           "empty References header.\n"
           "#    5. time-posted. This is a time_t (see "
           "http://en.wikipedia.org/wiki/Unix_time)\n"
           "#    6. xref line, server1:group1:number1 server2:group2:number2 "
           "...\n"
           "#    7. has-attachments [parts-total-count parts-found-count] "
           "line-count\n"
           "#       If has-attachments isn't 't' (for true), fields 2 and 3 "
           "are omitted.\n"
           "#       If fields 2 and 3 are equal, the article is `complete'.\n"
           "#    8. One line per parts-found-count: part-index message-id "
           "byte-count\n"
           "#\n"
           "#\n";

    // lines moved from line 8 to line 7 in 0.115, causing version 2
    // flag added, version 3 (12/2011, imhotep)
    out << "3\t # file format version number\n";

    // xref lookup section
    frequency_t frequency;
    foreach_const (std::vector<Article *>, articles, ait)
    {
      foreach_const (Xref, (*ait)->xref, xit)
      {
        ++frequency[xit->group];
      }
    }
    QuarkToSymbol xref_qts;
    build_qts(frequency, xref_qts);
    xref_qts.write(out, "xref shorthand count");

    // author lookup section
    frequency.clear();
    foreach_const (std::vector<Article *>, articles, ait)
    {
      ++frequency[(*ait)->author];
    }
    QuarkToSymbol author_qts;
    build_qts(frequency, author_qts);
    author_qts.write(out, "author shorthand count");

    // header section
    out << articles.size() << endl;
    std::string references;
    foreach_const (std::vector<Article *>, articles, ait)
    {
      ++article_count;

      Article const *a(*ait);
      Quark const &message_id(a->message_id);
      h->build_references_header(a, references);

      // flag, message-id, subject, author
      out << a->flag << "\n"
          << message_id << "\n\t" << a->subject << "\n\t"
          << author_qts(a->author) << "\n\t";
      // references line *IF* the article has a References header
      if (! references.empty())
      {
        out << references << "\n\t";
      }

      // date
      out << a->time_posted << "\n\t";

      // xref
      foreach_const (Xref, a->xref, xit)
      {
        out << xref_qts(xit->server);
        out.put(':');
        out << xref_qts(xit->group);
        out.put(':');
        out << xit->number;
        out.put(' ');
      }
      out << "\n\t";

      // is_binary [total_part_count found_part_count]
      out.put(a->is_binary ? 't' : 'f');
      if (a->is_binary)
      {
        out.put(' ');
        out << a->get_total_part_count();
        out.put(' ');
        out << a->get_found_part_count();
      }
      out.put(' ');
      out << a->lines;
      out.put('\n');

      // one line per foundPartCount (part-index message-id bytes lines)
      for (Article::part_iterator pit(a->pbegin()), end(a->pend()); pit != end;
           ++pit)
      {
        out.put('\t');
        out << pit.number();
        out.put(' ');
        out << pit.mid();
        out.put(' ');
        out << pit.bytes();
        out.put('\n');
        ++part_count;
      }
    }

    success = ! out.fail();
    data_io.write_done(&out);
    save_group_xovers (data_io);
  }

  return success;
}

void DataImpl ::save_headers(DataIO &data_io, Quark const &group) const
{
  if (_unit_test)
  {
    return;
  }

  pan_return_if_fail(! group.empty());

  TimeElapsed timer;

  // get a list of the articles
  GroupHeaders const *h(get_group_headers(group));
  std::vector<Article *> articles;
  foreach_const (nodes_t, h->_nodes, it)
  {
    if (it->second->_article)
    {
      articles.push_back(it->second->_article);
    }
  }

  unsigned long part_count(0ul);
  unsigned long article_count(0ul);
  bool const success(
    save_headers(data_io, group, articles, part_count, article_count));
  double const time_elapsed(timer.get_seconds_elapsed());
  if (success)
  {
    Log::add_info_va(_("Saved %lu parts, %lu articles in \"%s\" in %.1f "
                       "seconds (%.0f articles/sec)"),
                     part_count,
                     article_count,
                     group.c_str(),
                     time_elapsed,
                     article_count
                       / (fabs(time_elapsed) < 0.001 ? 0.001 : time_elapsed));
  }
}

/*******
********
*******/
namespace {
/* autosave newsrc files */
gboolean nrc_as_cb(gpointer ptr)
{
  DataImpl *data = static_cast<DataImpl *>(ptr);
  data->save_newsrc_files();

  return FALSE;
}
} // namespace

void DataImpl ::mark_read(Article const &a, bool read)
{
  Article const *aptr(&a);
  mark_read(&aptr, 1, read);
}

void DataImpl ::mark_read(Article const **articles,
                          unsigned long article_count,
                          bool read)
{
  typedef std::map<Quark, quarks_t> group_to_changed_mids_t;
  group_to_changed_mids_t group_to_changed_mids;

  // set them to `read'...
  for (Article const **it(articles), **end(articles + article_count); it != end;
       ++it)
  {
    Article const *article(*it);
    foreach_const (Xref, article->xref, xit)
    {
      bool const old_state(_read_groups[xit->group][xit->server]._read.mark_one(
        xit->number, read));
      if (! old_state != ! read)
      {
        group_to_changed_mids[xit->group].insert(article->message_id);
      }
    }
  }

  // update the affected groups' unread counts...
  foreach_const (group_to_changed_mids_t, group_to_changed_mids, it)
  {
    Quark const &group(it->first);
    ReadGroup &g(_read_groups[group]);
    const Article_Count n{it->second.size()};
    if (read)
    {
      g.decrement_unread(n);
    }
    else
    {
      g._unread_count += n;
    }
    fire_group_counts(group, g._unread_count, g._article_count);
    on_articles_changed(group, it->second, false);
  }

  if (! newsrc_autosave_id && newsrc_autosave_timeout)
  {
    newsrc_autosave_id =
      g_timeout_add_seconds(newsrc_autosave_timeout * 60, nrc_as_cb, this);
  }
}

bool DataImpl ::is_read(Article const *a) const
{
  // if it's read on any server, the whole thing is read.
  if (a != nullptr)
  {
    foreach_const (Xref, a->xref, xit)
    {
      ReadGroup::Server const *rgs(
        find_read_group_server(xit->group, xit->server));
      if (rgs && rgs->_read.is_marked(xit->number))
      {
        return true;
      }
    }
  }

  return false;
}

void DataImpl ::get_article_scores(Quark const &group,
                                   Article const &article,
                                   Scorefile::items_t &setme) const
{
  ArticleFilter ::sections_t sections;
  _scorefile.get_matching_sections(StringView(group), sections);
  _article_filter.get_article_scores(*this, sections, group, article, setme);
}

void DataImpl ::rescore_articles(Quark const &group, const quarks_t mids)
{

  GroupHeaders *gh(get_group_headers(group));
  if (! gh) // group isn't loaded
  {
    return;
  }

  ArticleFilter::sections_t sections;
  _scorefile.get_matching_sections(group.to_view(), sections);
  nodes_v nodes;
  find_nodes(mids, gh->_nodes, nodes);
  foreach (nodes_v, nodes, it)
  {
    if ((*it)->_article)
    {
      Article &a(*(*it)->_article);
      a.score = _article_filter.score_article(*this, sections, group, a);
    }
  }
}

void DataImpl ::rescore_group_articles(Quark const &group)
{

  GroupHeaders *gh(get_group_headers(group));
  if (! gh) // group isn't loaded
  {
    return;
  }

  ArticleFilter::sections_t sections;
  _scorefile.get_matching_sections(group.to_view(), sections);
  foreach (nodes_t, gh->_nodes, it)
  {
    if (it->second->_article)
    {
      Article &a(*(it->second->_article));
      a.score = _article_filter.score_article(*this, sections, group, a);
    }
  }
}

void DataImpl ::rescore()
{
  // std::cerr << LINE_ID << " rescoring... " << std::endl;
  const std::string filename(_data_io->get_scorefile_name());

  // reload the scorefile...
  _scorefile.clear();
  _scorefile.parse_file(filename);

  // enumerate the groups that need rescoring...
  quarks_t groups;
  foreach (std::set<MyTree *>, _trees, it)
  {
    groups.insert((*it)->_group);
  }

  // "on_articles_changed" rescores the articles...
  foreach_const (quarks_t, groups, git)
  {
    quarks_t mids;
    Quark const &group(*git);
    GroupHeaders const *h(get_group_headers(group));
    foreach_const (nodes_t, h->_nodes, nit)
    {
      // only insert mids for nodes with articles
      if (nit->second->_article)
      {
        mids.insert(mids.end(), nit->first);
      }
    }
    if (! mids.empty())
    {
      on_articles_changed(group, mids, true);
    }
  }
}

void DataImpl ::add_score(StringView const &section_wildmat,
                          int score_value,
                          bool score_assign_flag,
                          int lifespan_days,
                          bool all_items_must_be_true,
                          Scorefile::AddItem const *items,
                          size_t item_count,
                          bool do_rescore)
{
  const std::string filename(_data_io->get_scorefile_name());

  if (item_count && items)
  {
    // append to the file...
    const std::string str(_scorefile.build_score_string(section_wildmat,
                                                        score_value,
                                                        score_assign_flag,
                                                        lifespan_days,
                                                        all_items_must_be_true,
                                                        items,
                                                        item_count));
    std::ofstream o(filename.c_str(), std::ofstream::app | std::ofstream::out);
    o << '\n' << str << '\n';
    o.close();
    chmod(filename.c_str(), 0600);
  }

  if (do_rescore)
  {
    rescore();
  }
}

void DataImpl ::comment_out_scorefile_line(StringView const &filename,
                                           size_t begin_line,
                                           size_t end_line,
                                           bool do_rescore)
{
  std::string buf;

  // read the file in...
  std::string line;
  std::ifstream in(filename.to_string().c_str());
  size_t line_number(0);
  while (std::getline(in, line))
  {
    ++line_number;
    if (begin_line <= line_number && line_number <= end_line)
    {
      buf += '#';
    }
    buf += line;
    buf += '\n';
  }
  in.close();

  // ..and back out again
  const std::string f(filename.str, filename.len);
  std::ofstream o(f.c_str(), std::ofstream::trunc | std::ofstream::out);
  o << buf;
  o.close();
  chmod(f.c_str(), 0600);

  // rescore
  if (do_rescore)
  {
    rescore();
  }
}

/***************************************************************************
****************************************************************************
***************************************************************************/

namespace {
/** used by delete_articles */
struct PerGroup
{
    quarks_t mids;
    Article_Count unread;
    Article_Count count;

    PerGroup() :
      unread(0),
      count(0)
    {
    }
};
} // namespace

void DataImpl ::group_clear_articles(Quark const &group)
{
  // if they're in memory, remove them from there too...
  GroupHeaders *headers(get_group_headers(group));
  if (headers)
  {
    unique_articles_t all;
    foreach (nodes_t, headers->_nodes, it)
    {
      if (it->second->_article)
      {
        all.insert(it->second->_article);
      }
    }
    delete_articles(all);
  }

  // reset GroupHeaders' memory...
  //  headers->_nodes.clear ();
  //  headers->_node_chunk.clear ();
  //  headers->_art_chunk.clear ();

  // remove 'em from disk too.
  _data_io->clear_group_headers(group);

  // fire a 'count changed' event.
  ReadGroup &g(_read_groups[group]);
  g._article_count = static_cast<Article_Count>(0);
  g._unread_count = static_cast<Article_Count>(0);
  fire_group_counts(group, g._unread_count, g._article_count);
}

void DataImpl ::delete_articles(unique_articles_t const &articles)
{

  quarks_t all_mids;

  // info we need to batch these deletions per group...
  typedef std::map<Quark, PerGroup> per_groups_t;
  per_groups_t per_groups;

  // populate the per_groups map
  foreach_const (unique_articles_t, articles, it)
  {
    Article const *article(*it);
    quarks_t groups;
    foreach_const (Xref, article->xref, xit)
    {
      groups.insert(xit->group);
    }
    bool const was_read(is_read(article));
    foreach_const (quarks_t, groups, git)
    {
      PerGroup &per(per_groups[*git]);
      ++per.count;
      if (! was_read)
      {
        ++per.unread;
      }
      per.mids.insert(article->message_id);
      all_mids.insert(article->message_id);
    }
  }

  // process each group
  foreach (per_groups_t, per_groups, it)
  {
    // update the group's read/unread count...
    Quark const &group(it->first);
    ReadGroup &g(_read_groups[group]);
    g.decrement_unread(it->second.unread);
    g.decrement_count(it->second.count);
    fire_group_counts(group, g._unread_count, g._article_count);

    // remove the articles from our lookup table...
    GroupHeaders *h(get_group_headers(group));
    if (h)
    {
      h->remove_articles(it->second.mids);
    }
  }

  on_articles_removed(all_mids);
}

void DataImpl ::on_articles_removed(quarks_t const &mids) const
{
  foreach (std::set<MyTree *>, _trees, it)
  {
    (*it)->remove_articles(mids);
  }
}

void DataImpl ::on_articles_changed(Quark const &group,
                                    quarks_t const &mids,
                                    bool do_refilter)
{
  rescore_articles(group, mids);

  // notify the trees that the articles have changed...
  foreach (std::set<MyTree *>, _trees, it)
  {
    (*it)->articles_changed(mids, do_refilter);
  }
}

void DataImpl ::on_articles_added(Quark const &group, quarks_t const &mids)
{

  if (! mids.empty())
  {
    Log::add_info_va(
      _("Added %lu articles to %s."), mids.size(), group.c_str());

    rescore_articles(group, mids);

    foreach (std::set<MyTree *>, _trees, it)
    {
      pan_debug("This tree has a group " << (*it)->_group);
      if ((*it)->_group == group)
      {
        pan_debug("trying to add the articles to tree " << *it);
        (*it)->add_articles(mids);
      }
    }

    ReadGroup &g(_read_groups[group]);
    g._article_count += mids.size();
    g._unread_count += mids.size();
    fire_group_counts(group, g._unread_count, g._article_count);
  }
}

DataImpl::ArticleNode *DataImpl ::find_ancestor(ArticleNode *node,
                                                Quark const &ancestor_mid)
{
  ArticleNode *parent_node(node->_parent);
  while (parent_node && (parent_node->_mid != ancestor_mid))
  {
    parent_node = parent_node->_parent;
  }
  return parent_node;
}

DataImpl::ArticleNode *DataImpl ::find_closest_ancestor(
  ArticleNode *node, unique_sorted_quarks_t const &mid_pool)
{
  ArticleNode *parent_node(node->_parent);
  while (parent_node && ! mid_pool.count(parent_node->_mid))
  {
    parent_node = parent_node->_parent;
  }
  return parent_node;
}

DataImpl::ArticleNode const *DataImpl ::find_closest_ancestor(
  ArticleNode const *node, unique_sorted_quarks_t const &mid_pool)
{
  ArticleNode const *parent_node(node->_parent);
  while (parent_node && ! mid_pool.count(parent_node->_mid))
  {
    parent_node = parent_node->_parent;
  }
  return parent_node;
}

Data::ArticleTree *DataImpl ::group_get_articles(Quark const &group,
                                                 Quark const &save_path,
                                                 const ShowType show_type,
                                                 FilterInfo const *filter,
                                                 RulesInfo const *rules) const
{
  // cast const away for group_ref()... consider _groups mutable
  return new MyTree(
    *const_cast<DataImpl *>(this), group, save_path, show_type, filter, rules);
}
