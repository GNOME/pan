/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007  Charles Kerr <charles@rebelbase.com>
 *
 * This File:
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 * Copyright (C) 2007 Calin Culianu <calin@ajvar.org>
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
#include <algorithm>
#include <cassert>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cstdio>

#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/gnksa.h>
#include "task-multipost.h"
#include "nzb.h"

using namespace pan;

namespace
{
  std::string get_description (const char* name)
  {
    char buf[4096];
    snprintf (buf, sizeof(buf), _("Uploading \"%s\""), name);
    return buf;
  }

  std::string g_get_basename(const char* f)
  {
    char buf[4096];
    char * freeme = g_path_get_basename(f);
    snprintf (buf, sizeof(buf), "%s", freeme);
    g_free(freeme);
    return buf;
  }
}

/***
****
***/

TaskMultiPost :: TaskMultiPost (quarks_t            & filenames,
                                const Quark         & server,
                                Article               article,
                                GMimeMultipart      * msg,
                                Progress::Listener  * listener):
   Task ("UPLOAD", get_description(article.subject.to_string().c_str())),
  _filenames(filenames),
  _server(server),
  _article(article),
  _subject (article.subject.to_string()),
  _author(article.author.to_string()),
  _files_left(1),
  _msg(msg)

{
  g_mime_multipart_set_boundary(_msg, "$pan_multipart_msg$");
  build_needed_tasks();
  //  _state.set_paused();
  update_work();
}

void
TaskMultiPost :: build_needed_tasks()
{
  _files_left = _needed.size();
  foreach (needed_t, _needed, it)
  {
    _mids.push_back(Quark(it->second.message_id));
  }

}

void
TaskMultiPost :: update_work (NNTP* checkin_pending)
{

  int working(0);
  foreach (needed_t, _needed, nit)
  {
    TaskMultiPost::Needed& n (nit->second);
    if (n.nntp && n.nntp!=checkin_pending)
      ++working;
  }

  if (working)
  {
    _state.set_working();
  }
  else if (_files_left==0)
  {
//    prepare_msg();
    _state.set_need_nntp(_server);
  }
  else if (!working && _files_left == 0)
  {
    _state.set_completed();
    set_finished(OK);
  }
}


void
TaskMultiPost :: use_nntp (NNTP * nntp)
{
  char * pch = g_mime_object_to_string ((GMimeObject *) _msg);
  nntp->post(pch, this);
  g_free(pch);

  update_work ();
}

/***
****
***/

void
TaskMultiPost :: on_nntp_line (NNTP * nntp,
                              const StringView & line_in) {}

void
TaskMultiPost :: on_nntp_done (NNTP * nntp,
                             Health health,
                             const StringView & response)
{

  update_work(nntp);
  check_in (nntp, health);

}

/***
****
***/

unsigned long
TaskMultiPost :: get_bytes_remaining () const
{
  return 0;
}


void
TaskMultiPost :: stop ()
{
}


TaskMultiPost :: ~TaskMultiPost ()
{
  g_object_unref(_msg);

}





void
TaskMultiPost :: dbg()
{
  std::cerr<<g_mime_object_to_string((GMimeObject*)_msg)<<std::endl;
  std::cerr<<"\n////////////////////////////////////\n";
  foreach_const (quarks_t, _filenames, it)
    std::cerr<<*it<<std::endl;
}
