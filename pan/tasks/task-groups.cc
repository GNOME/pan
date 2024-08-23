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

#include <config.h>
#include <cassert>
#include <glib/gi18n.h>
extern "C" {
  #include <stdlib.h>
}
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/utf8-utils.h>
#include <pan/data/data.h>
#include "task-groups.h"

using namespace pan;

namespace
{
  std::string get_description (const Data& data, const Quark& server)
  {
    char buf[1024];
    std::string host (data.get_server_address (server));
    snprintf (buf, sizeof(buf), _("Getting group list from \"%s\""), host.c_str());
    return std::string (buf);
  }
}

TaskGroups :: TaskGroups (Data& data, const Quark& servername):
   Task ("GROUPS", get_description(data,servername)),
   _data (data),
   _servername (servername),
   _group_count (0),
   _step (LIST)
{
   _state.set_need_nntp (servername);
}

TaskGroups :: ~TaskGroups ()
{}

/***
****
***/

void
TaskGroups :: use_nntp (NNTP * nntp)
{
  pan_debug ("groups task got an nntp " << nntp->_server << "; step is " << _step);

  _state.set_working ();

  if (_step == LIST) // "LIST" for a full list of groups...
    nntp->list (this);
  else if (_step == LIST_NEWSGROUPS) // "LIST NEWSGROUPS" for descriptions...
    nntp->list_newsgroups (this);
  else
    assert (0);
}

void
TaskGroups :: on_nntp_line (NNTP               * nntp,
                            const StringView   & line)
{
  // gzip compression
  if (nntp->_compression)
  {
    stream<<line;
  }
  else on_nntp_line_process (nntp, line);
}
void
TaskGroups :: on_nntp_line_process (NNTP               * nntp UNUSED,
                                    const StringView   & line)
{
  char permission ('y');
  std::string name_str;
  std::string desc_str;

  if (_step == LIST)
  {
    StringView myline(line), tmp, post;
    myline.pop_token (tmp);
    name_str = content_to_utf8 (tmp);
    myline.pop_token (tmp); // skip low number
    myline.pop_token (tmp); // skip high number
    myline.pop_token (post); // ok to post?
    if (!post.empty())
      permission = tolower (*post.str);
  }
  else // LIST_NEWSGROUPS
  {
    const char * pch = line.begin();
    while (pch!=line.end() && !isspace((int)*pch)) ++pch;
    name_str = content_to_utf8 (StringView (line.str, pch-line.str));
    desc_str = content_to_utf8 (StringView (pch, line.str+line.len-pch));
  }

  StringView name (name_str);
  StringView desc (desc_str);
  name.trim ();
  desc.trim ();

  if (!name.empty()) {
    const Quark name_quark (name);
    Data::NewGroup& ng (_new_groups[name_quark]);
    if (ng.group.empty())
        ng.group = name_quark;
    if (ng.description.empty())
        ng.description.assign (desc.str, desc.len);
    if (ng.permission == '?')
        ng.permission = permission;
  }

  if (!(++_group_count % 100ul)) {
    char buf[1024];
    snprintf (buf, sizeof(buf), _("Fetched %lu Groups"), _group_count);
    set_status (buf);
  }

  increment_step ();
}

void
TaskGroups :: on_nntp_done (NNTP              * nntp,
                            Health              health,
                            const StringView  & response)
{
  pan_debug ("groups task got an on_nntp_done() from " << nntp->_server);

  if (health == ERR_NETWORK)
  {
    _state.set_need_nntp (_servername);
  }
  else // health is OK or FAIL
  {

    const Quark& server(nntp->_server);
    CompressionType comp;
    _data.get_server_compression_type(server, comp);
    const bool is_gzipped (comp == HEADER_COMPRESS_XFEATURE);

    if (is_gzipped)
    {
      std::ofstream of("tmp_out");
      of << stream.str();
      of.close();
      std::stringstream out,out2;
      bool fail = !compression::inflate_zlib(&stream, &out, comp);
      if (!fail)
      {
        char buf[4096];
        while (true)
        {
          std::istream& str = out.getline(buf, sizeof(buf));
          if (str.fail() || str.bad() || str.eof()) break;
          on_nntp_line_process(nntp, buf);
        }
      } else
      {
        _state.set_completed();
        set_finished(ERR_LOCAL);
      }

    }

    if (_step == LIST_NEWSGROUPS)
    {
      int i (0);
      Data::NewGroup * ng = new Data::NewGroup [_new_groups.size()];
      foreach_const (new_groups_t, _new_groups, it)
        ng[i++] = it->second;
      _data.add_groups (_servername, ng, i);
      delete [] ng;

      pan_debug ("groups task setting state completed");
      _state.set_completed ();
      set_finished (OK);
    }
    else // _step == LIST
    {
      _state.set_need_nntp (_servername);
      _step = LIST_NEWSGROUPS;
    }
  }

  check_in (nntp, health);
}
