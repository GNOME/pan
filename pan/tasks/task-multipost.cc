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
    char * freeme = g_path_get_basename(name);
    snprintf (buf, sizeof(buf), _("Uploading %s"), freeme);
    g_free(freeme);
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

TaskMultiPost :: TaskMultiPost (const std::string   & filename,
                          const Quark               & server,
                          Article                     article,
                          GMimeMessage *              msg,
                          Progress::Listener        * listener):
  Task ("UPLOAD", get_description(filename.c_str())),
  _filename(filename),
  _basename (g_get_basename(filename.c_str())),
  _server(server),
  _article(article),
  _subject (article.subject.to_string()),
  _author(article.author.to_string()),
  _msg (msg)
{


//  struct stat sb;
//  stat(filename.c_str(),&sb);
//  _bytes = sb.st_size;

}

void
TaskMultiPost :: build_needed_tasks()
{

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
    TaskUpload::Needed& n (nit->second);
    if (n.nntp && n.nntp!=checkin_pending)
      ++working;
  }

  /* only need encode if mode is NOT plain */
  if (working)
  {
    _state.set_working();
  }
  else if (!working)
  {
    _state.set_need_nntp(_server);
  }
  else if (_needed.empty())
  {
    _state.set_completed();
    set_finished(OK);
  }
}


void
TaskMultiPost :: use_nntp (NNTP * nntp)
{
  nntp->post(StringView(""), this);

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
  unsigned long bytes (0);
  foreach_const (needed_t, _needed, it)
    bytes += (unsigned long)it->second.bytes;
  return bytes;
}


void
TaskMultiPost :: stop ()
{
}


TaskMultiPost :: ~TaskMultiPost ()
{


}
