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
  #include <zlib.h>
}
#include <fstream>
#include <iostream>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/utf8-utils.h>
#include <pan/data/data.h>
#include "nntp.h"
#include "task-xoverinfo.h"


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

  std::string get_description (const Quark& group)
  {
    char buf[1024];
    snprintf (buf, sizeof(buf), _("Getting header counts for \"%s\""), group.c_str());
    return std::string (buf);
  }
}

TaskXOverInfo :: TaskXOverInfo (Data         & data,
                                const Quark  & group,
                                std::map<Quark,xover_t>& xovers) :
  Task("XOVER", get_description(group)),
  _data (data),
  _group (group),
  _short_group_name (get_short_name (StringView (group.c_str()))),
  _xovers(xovers)
{

  debug ("ctor for " << group);

  // add a ``GROUP'' MiniTask for each server that has this group
  // initialize the _high lookup table to boundaries
  const MiniTask group_minitask (MiniTask::GROUP);
  quarks_t servers;
  _data.group_get_servers (group, servers);

  foreach_const (quarks_t, servers, it)
  {
    if (_data.get_server_limits(*it))
    {
      _server_to_minitasks[*it].push_front (group_minitask);
      std::pair<uint64_t,uint64_t>& p (xovers[*it]);
      p.first = data.get_xover_high (group, *it);
    }
  }
  init_steps (0);

  update_work ();
}

TaskXOverInfo :: ~TaskXOverInfo ()
{}

void
TaskXOverInfo :: use_nntp (NNTP* nntp)
{
  const Quark& server (nntp->_server);
  debug ("got an nntp from " << nntp->_server);

  nntp->xover_count_only (_group, this);
}

/***
****
***/

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
TaskXOverInfo :: on_nntp_line         (NNTP               * nntp,
                                       const StringView   & line)
{
  uint64_t new_high(atoi(line.str));
//  nntp
}

void
TaskXOverInfo :: on_nntp_done (NNTP              * nntp,
                               Health              health,
                               const StringView  & response UNUSED)
{
  update_work (true);
  check_in (nntp, health);
}

void
TaskXOverInfo :: update_work (bool subtract_one_from_nntp_count)
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
