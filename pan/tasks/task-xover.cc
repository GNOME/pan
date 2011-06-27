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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <cassert>
#include <cerrno>
extern "C" {
  #define PROTOTYPES
  #include <stdio.h>
  #include <uulib/uudeview.h>
  #include <glib/gi18n.h>
  #include <gmime/gmime-utils.h>

}
#include <fstream>
#include <iostream>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/utf8-utils.h>
#include <pan/general/file-util.h>
#include <pan/data/data.h>
#include "nntp.h"
#include "task-xover.h"

using namespace pan;

namespace
{
   std::string
   get_short_name (const StringView& in)
   {
    static const StringView moderated ("moderated");
    static const StringView d ("d");

    StringView myline, long_token;

    // find the long token -- use the last, unless that's "moderated" or "d"
    myline = in;
    myline.pop_last_token (long_token, '.');
    if (!myline.empty() && (long_token==moderated || long_token==d))
      myline.pop_last_token (long_token, '.');

    // build a new string where each token is shortened except for long_token
    std::string out;
    myline = in;
    StringView tok;
    while (myline.pop_token (tok, '.')) {
      out.insert (out.end(), tok.begin(), (tok==long_token ? tok.end() : tok.begin()+1));
      out += '.';
    }
    if (!out.empty())
      out.erase (out.size()-1);

    return out;
  }

  std::string get_description (const Quark& group, TaskXOver::Mode mode)
  {
    char buf[1024];
    if (mode == TaskXOver::ALL)
      snprintf (buf, sizeof(buf), _("Getting all headers for \"%s\""), group.c_str());
    else if (mode == TaskXOver::NEW)
      snprintf (buf, sizeof(buf), _("Getting new headers for \"%s\""), group.c_str());
    else // SAMPLE
      snprintf (buf, sizeof(buf), _("Sampling headers for \"%s\""), group.c_str());
    return std::string (buf);
  }
}

namespace
{
    void create_cachename(char* in, size_t len, const char* add)
    {
        g_snprintf(in, len, "%s%c%s",file::get_pan_home().c_str(), G_DIR_SEPARATOR , add);
    }
}

TaskXOver :: TaskXOver (Data         & data,
                        const Quark  & group,
                        Mode           mode,
                        unsigned long  sample_size):
  Task("XOVER", get_description(group,mode)),
  _data (data),
  _group (group),
  _short_group_name (get_short_name (StringView (group.c_str()))),
  _mode (mode),
  _sample_size (sample_size),
  _days_cutoff (mode==DAYS ? (time(0)-(sample_size*24*60*60)) : 0),
  _group_xover_is_reffed (false),
  _bytes_so_far (0),
  _parts_so_far (0ul),
  _articles_so_far (0ul),
  _lines_so_far (0ul),
  _total_minitasks (0),
  _working(0)
{

  debug ("ctor for " << group);

  char buf[2048];
  create_cachename (buf,sizeof(buf), "headers");
  _headers.open(buf, std::ios::out | std::ios::trunc);

  // add a ``GROUP'' MiniTask for each server that has this group
  // initialize the _high lookup table to boundaries
  const MiniTask group_minitask (MiniTask::GROUP);
  quarks_t servers;
  _data.group_get_servers (group, servers);
  foreach_const (quarks_t, servers, it)
    if (_data.get_server_limits(*it))
    {
      _server_to_minitasks[*it].push_front (group_minitask);
      _high[*it] = data.get_xover_high (group, *it);
    }
  init_steps (0);

  // tell the users what we're up to
  set_status (group.c_str());

  update_work ();
}

TaskXOver :: ~TaskXOver ()
{
  if (_group_xover_is_reffed) {
    foreach (server_to_high_t, _high, it)
      _data.set_xover_high (_group, it->first, it->second);
    _data.xover_unref (_group);
  }
}

void
TaskXOver :: use_nntp (NNTP* nntp)
{
  const Quark& server (nntp->_server);
  debug ("got an nntp from " << nntp->_server);

  // if this is the first nntp we've gotten, ref the xover data
  if (!_group_xover_is_reffed) {
    _group_xover_is_reffed = true;
    _data.xover_ref (_group);
  }

  MiniTasks_t& minitasks (_server_to_minitasks[server]);
  if (minitasks.empty())
  {
    debug ("That's interesting, I got a socket for " << server << " but have no use for it!");
    _state._servers.erase (server);
    check_in (nntp, OK);
  }
  else
  {
    const MiniTask mt (minitasks.front());
    minitasks.pop_front ();
    switch (mt._type)
    {
      case MiniTask::GROUP:
        debug ("GROUP " << _group << " command to " << server);
        nntp->group (_group, this);
        break;
      case MiniTask::XOVER:
        debug ("XOVER " << mt._low << '-' << mt._high << " to " << server);
        _last_xover_number[nntp] = mt._low;
        nntp->xover (_group, mt._low, mt._high, this);
        break;
      default:
        assert (0);
    }
    update_work ();
  }
}

/***
****
***/

void
TaskXOver :: on_nntp_group (NNTP          * nntp,
                            const Quark   & group,
                            unsigned long   qty,
                            uint64_t        low,
                            uint64_t        high)
{
  const Quark& servername (nntp->_server);

  // new connections can tickle this...
  if (_servers_that_got_xover_minitasks.count(servername))
    return;

  _servers_that_got_xover_minitasks.insert (servername);

  debug ("got GROUP result from " << nntp->_server << " (" << nntp << "): "
         << " qty " << qty
         << " low " << low
         << " high " << high);

  uint64_t l(low), h(high);
  _data.set_xover_low (group, nntp->_server, low);
  //std::cerr << LINE_ID << " This group's range is [" << low << "..." << high << ']' << std::endl;

  if (_mode == ALL || _mode == DAYS)
    l = low;
  else if (_mode == SAMPLE) {
    _sample_size = std::min (_sample_size, high-low);
    //std::cerr << LINE_ID << " and I want to sample " <<  _sample_size << " messages..." << std::endl;
    l = std::max (low, high+1-_sample_size);
  }
  else { // NEW
    uint64_t xh (_data.get_xover_high (group, nntp->_server));
    //std::cerr << LINE_ID << " current xover high is " << xh << std::endl;
    l = std::max (xh+1, low);
  }

  if (l <= high)
  {
    //std::cerr << LINE_ID << " okay, I'll try to get articles in [" << l << "..." << h << ']' << std::endl;
    add_steps (h-l);
    const int INCREMENT (1000);
    MiniTasks_t& minitasks (_server_to_minitasks[servername]);
    for (uint64_t m=l; m<=h; m+=INCREMENT) {
      MiniTask mt (MiniTask::XOVER, m, m+INCREMENT);
      debug ("adding MiniTask for " << servername << ": xover [" << mt._low << '-' << mt._high << ']');
      minitasks.push_front (mt);
      ++_total_minitasks;
    }
  }
  else
  {
    //std::cerr << LINE_ID << " nothing new here..." << std::endl;
    _high[nntp->_server] = high;
  }
  _working = _total_minitasks;
}

namespace
{
  unsigned long view_to_ul (const StringView& view)
  {
    unsigned long ul = 0ul;

    if (!view.empty()) {
      errno = 0;
      ul = strtoul (view.str, 0, 10);
      if (errno)
        ul = 0ul;
    }

    return ul;
  }
  uint64_t view_to_ull (const StringView& view)
  {
    uint64_t ul = 0ul;

    if (!view.empty()) {
      errno = 0;
      ul = g_ascii_strtoull (view.str, 0, 10);
      if (errno)
        ul = 0ul;
    }

    return ul;
  }

  bool header_is_nonencoded_utf8 (const StringView& in)
  {
    const bool is_nonencoded (!in.strstr("=?"));
    const bool is_utf8 (g_utf8_validate (in.str, in.len, 0));
    return is_nonencoded && is_utf8;
  }
}

void
TaskXOver :: on_nntp_line         (NNTP               * nntp,
                                   const StringView   & line)
{
    _headers<<line<<"\r\n";
    increment_step(1);
    ++_lines_so_far;
    _bytes_so_far += line.len;

    if (!(_lines_so_far % 500))
     set_status_va (_("%s (%lu Header Lines)"), _short_group_name.c_str(), _lines_so_far);

}

void
TaskXOver :: on_nntp_line_process (NNTP               * nntp,
                                   const StringView   & line)
{

  pan_return_if_fail (nntp != 0);
  pan_return_if_fail (!nntp->_server.empty());
  pan_return_if_fail (!nntp->_group.empty());

//  _bytes_so_far += line.len;

  unsigned int lines=0u;
  unsigned long bytes=0ul;
  uint64_t number=0;
  StringView subj, author, date, mid, ref, tmp, xref, l(line);
  bool ok = !l.empty();
  ok = ok && l.pop_token (tmp, '\t');    if (ok) number = view_to_ull (tmp);
  ok = ok && l.pop_token (subj, '\t');   if (ok) subj.trim ();
  ok = ok && l.pop_token (author, '\t'); if (ok) author.trim ();
  ok = ok && l.pop_token (date, '\t');   if (ok) date.trim ();
  ok = ok && l.pop_token (mid, '\t');    if (ok) mid.trim ();
  ok = ok && l.pop_token (ref, '\t');    if (ok) ref.trim ();
  ok = ok && l.pop_token (tmp, '\t');    if (ok) bytes = view_to_ul (tmp);
  ok = ok && l.pop_token (tmp, '\t');    if (ok) lines = view_to_ul (tmp);
  ok = ok && l.pop_token (xref, '\t');   if (ok) xref.trim ();

  if (xref.len>6 && !strncmp(xref.str,"Xref: ", 6)) {
    xref = xref.substr (xref.str+6, 0);
    xref.trim ();
  }

  // is this header corrupt?
  if (!number // missing number
      || subj.empty() // missing subject
      || author.empty() // missing author
      || date.empty() // missing date
      || mid.empty() // missing mid
      || mid.front()!='<' // corrupt mid
      || (!ref.empty() && ref.front()!='<'))
    return;

  // if news server doesn't provide an xref, fake one
  char * buf (0);
  if (xref.empty())
    xref = buf = g_strdup_printf ("%s %s:%"G_GUINT64_FORMAT,
                                  nntp->_server.c_str(),
                                  nntp->_group.c_str(),
                                  number);

  uint64_t& h (_high[nntp->_server]);
  h = std::max (h, number);

  const char * fallback_charset = NULL; // FIXME

  // are we done?
  const time_t time_posted = g_mime_utils_header_decode_date (date.str, NULL);
  if( _mode==DAYS && time_posted<_days_cutoff ) {
    _server_to_minitasks[nntp->_server].clear ();
    return;
  }

//  ++_parts_so_far;

  const Article * article = _data.xover_add (
    nntp->_server, nntp->_group,
    (header_is_nonencoded_utf8(subj) ? subj : header_to_utf8(subj,fallback_charset).c_str()),
    (header_is_nonencoded_utf8(author) ? author : header_to_utf8(author,fallback_charset).c_str()),
    time_posted, mid, ref, bytes, lines, xref);

//  if (article)
//    ++_articles_so_far;

  // emit a status update
//  uint64_t& prev = _last_xover_number[nntp];
//  increment_step (number - prev);
//  prev = number;
//  if (!(_parts_so_far % 500))
//    set_status_va (_("%s (%lu parts, %lu articles)"), _short_group_name.c_str(), _parts_so_far, _articles_so_far);

  // cleanup
  g_free (buf);
}

void
TaskXOver :: on_nntp_done (NNTP              * nntp,
                           Health              health,
                           const StringView  & response UNUSED)
{
  update_work (true);
  check_in (nntp, health);

  --_working;

  if (_working == 0)
  {
      char buf[2048];
      create_cachename(buf,2048,"headers");
      _headers.close();
      _headers.open(buf, std::ifstream::in);
      while (_headers.getline(buf,2048))
        on_nntp_line_process(nntp,StringView(buf));
  }
}

void
TaskXOver :: update_work (bool subtract_one_from_nntp_count)
{
  int nntp_count (get_nntp_count ());
  if (subtract_one_from_nntp_count)
    --nntp_count;

  // find any servers we still need
  quarks_t servers;
  foreach_const (server_to_minitasks_t, _server_to_minitasks, it)
    if (!it->second.empty())
      servers.insert (it->first);

  //std::cerr << LINE_ID << " servers: " << servers.size() << " nntp: " << nntp_count << std::endl;

  if (!servers.empty())
    _state.set_need_nntp (servers);
  else if (nntp_count)
    _state.set_working ();
  else {
    _state.set_completed();
    set_finished(OK);
  }
}

unsigned long
TaskXOver :: get_bytes_remaining () const
{
  unsigned int minitasks_left (0);
  foreach_const (server_to_minitasks_t, _server_to_minitasks, it)
    minitasks_left += it->second.size();

  const double percent_done (_total_minitasks ? (1.0 - minitasks_left/(double)_total_minitasks) : 0.0);
  if (percent_done < 0.1) // impossible to estimate
    return 0;
  const unsigned long total_bytes = (unsigned long)(_bytes_so_far / percent_done);
  return total_bytes - _bytes_so_far;
}
