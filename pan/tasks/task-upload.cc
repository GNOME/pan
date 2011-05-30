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
extern "C" {
#include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/data/article-cache.h>
#include "encoder.h"
#include "task-upload.h"

using namespace pan;

namespace
{
  std::string get_description (const char* name)
  {
    char buf[1024];
    char * freeme = g_path_get_basename(name);
    snprintf (buf, sizeof(buf), _("Uploading %s"), freeme);
    g_free(freeme);
    return buf;
  }

  std::string get_basename(const char* f)
  {
    char buf[1024];
    char * freeme = g_path_get_basename(f);
    snprintf (buf, sizeof(buf), _("%s"), freeme);
    g_free(freeme);
    return buf;
  }
}


namespace
{
  void delete_cache(const TaskUpload::Needed& n)
  {
    unlink(n.filename.c_str());
  }
}

/***
****
***/

TaskUpload :: TaskUpload ( const std::string         & filename,
                           const Quark               & server,
                           quarks_t                  & groups,
                           std::string                 subject,
                           std::string                 author,
                           Progress::Listener        * listener,
                           const TaskUpload::EncodeMode  enc):
  Task ("UPLOAD", get_description(filename.c_str())),
  _filename(filename),
  _basename (g_path_get_basename(filename.c_str())),
  _server(server),
  _groups(groups),
  _subject (subject),
  _author(author),
  _encoder(0),
  _encoder_has_run (false),
  _encode_mode(enc)
{
  if (listener != 0)
    add_listener (listener);

  struct stat sb;
  stat(filename.c_str(),&sb);
  _bytes = sb.st_size;
  update_work ();
}

void
TaskUpload :: update_work (NNTP* checkin_pending)
{

  int working(0);
  foreach (needed_t, _needed, nit) {
    Needed& n (nit->second);
    if (n.nntp && n.nntp!=checkin_pending)
      ++working;
  }

  if (!_encoder && !_encoder_has_run )
  {
    _state.set_need_encoder();
  } else if(working )
  {
    _state.set_working();
  } else if (_encoder_has_run && !_needed.empty())
  {
    _state.set_need_nntp(_server);
  } else if (_needed.empty() && _encoder_has_run )
  {
    _state.set_completed();
    set_finished(OK);
  }
}

/***
****
***/

void
TaskUpload :: use_nntp (NNTP * nntp)
{

  Needed * needed (0);
  for (needed_t::iterator it(_needed.begin()), end(_needed.end()); !needed && it!=end; ++it)
    if (it->second.nntp==0)
      needed = &it->second;

  if (!needed)
  {
    update_work (nntp);
    check_in (nntp, OK);
  }
  else
  {
    needed->nntp = nntp;
    set_status_va (_("Uploading \"%s\" - Part %d of %d"), _basename.c_str(), needed->partno, _total_parts);
    std::stringstream tmp;
    std::ifstream in(needed->filename.c_str(), std::ifstream::in);
    char tmp_char;
    while (in.good()) {
      tmp << (tmp_char=(char) in.get());
    }
    in.close();
    nntp->post(StringView(tmp.str()), this);
    update_work ();
  }
}

/***
****
***/

void
TaskUpload :: on_nntp_line  (NNTP               * nntp,
                              const StringView   & line_in)
{}

void
TaskUpload :: on_nntp_done  (NNTP             * nntp,
                             Health             health,
                             const StringView & response)
{

  needed_t::iterator it;
  for (it=_needed.begin(); it!=_needed.end(); ++it)
    if (it->second.nntp == nntp)
      break;

  switch (health)
  {
    case OK:
      break;
    /* if the connection was aborted with SIGPIPE, let this error silently by and
     * let GIOSocket code handle this */
    case ERR_NETWORK:
      std::cerr<<"err network!\n";
      it->second.reset();
      goto _end;
  }

  switch (atoi(response.str))
  {
    case NO_POSTING:
      Log :: add_err(_("Posting failed: No Posts allowed by server."));
      this->stop();
      break;
    case POSTING_FAILED:
      Log :: add_err_va (_("Posting of file %s failed: %s"), _basename.c_str(), response.str);
      break;
    case ARTICLE_POSTED_OK:
      delete_cache(it->second);
      _needed.erase (it);
      increment_step(1);
      if (_needed.empty())
        Log :: add_info_va(_("Posting of file %s succesful: %s"),
               _basename.c_str(), response.str);
      break;
    case TOO_MANY_CONNECTIONS:
      // lockout for 120 secs, but try
      _state.set_need_nntp(nntp->_server);
      break;
    default:
      Log :: add_err_va (_("Got unknown response code: %s"),response.str);
      break;
  }

  _end:
  update_work(nntp);
  check_in (nntp, health);

}

/***
****
***/

unsigned long
TaskUpload :: get_bytes_remaining () const
{
  unsigned long bytes (0);
  foreach_const (needed_t, _needed, it)
    bytes += it->second.bytes;
  return bytes;
}


void
TaskUpload :: use_encoder (Encoder* encoder)
{
  if (_state._work != NEED_ENCODER)
    check_in (encoder);

  _encoder = encoder;
  init_steps(100);
  _state.set_working();
  // build group name
  std::string groups;
  quarks_t::iterator it = _groups.begin();
  int i(0);
  for (; it != _groups.end(); ++it, ++i)
  {
    if (i<_groups.size()&& i>0 && _groups.size()>1) groups += ",";
    groups += (*it).to_string();
  }
  _encoder->enqueue (this, _filename, _basename, groups, _subject, _author, YENC);
  debug ("encoder thread was free, enqueued work");
}

void
TaskUpload :: stop ()
{
//  if (_encoder)
//      _encoder->cancel();
}

// called in the main thread by WorkerPool
void
TaskUpload :: on_worker_done (bool cancelled)
{
  assert(_encoder);
  if (!_encoder) return;

  if (!cancelled)
  {
    // the encoder is done... catch up on all housekeeping
    // now that we're back in the main thread.

    foreach_const(Encoder::log_t, _encoder->log_severe, it)
      Log :: add_err(it->c_str());
    foreach_const(Encoder::log_t, _encoder->log_errors, it)
      Log :: add_err(it->c_str());
    foreach_const(Encoder::log_t, _encoder->log_infos, it)
      Log :: add_info(it->c_str());

    int num_errors = _encoder->log_severe.size() +
                     _encoder->log_errors.size();

    if (!_encoder->log_errors.empty())
      set_error (_encoder->log_errors.front());

    if (num_errors>0)
    {
      /* stop state if encoder has encountered errors */
      _state.set_completed ();
      set_finished(ERR_LOCAL);
    }
    else
    {
      set_step (100);
      _encoder_has_run = true;
      _total_parts = _encoder->total_parts;
      /*enqueue all parts into the global needed_t list.
        update_work will then assign a pointer to the begin
        which will be used for an nntp upload.
        on nntp_done, the list is decreased by one member
       */
      Needed n;
      char buf[2048];
      struct stat sb;

      int total(0);
      /* not found: skip, we don't need that part
         found: if (partial) then assign the needed values
      */
      for (int i=1; i<=_total_parts; ++i)
      {
        needed_t::iterator it = _needed.find(i);
        if (it == _needed.end())
          continue;
        else if (it->second.partial)
        {
          n.partno = i;
          g_snprintf(buf,sizeof(buf),"%s/%s.%d",
                   file::get_uulib_path().c_str(),
                   _basename.c_str(), i);
          n.filename = buf;
          stat(buf, &sb);
          n.bytes = sb.st_size;
        }
          ++total;
      }

      if (total==0)
      {
        _state.set_completed();
        set_finished(OK);
      } else
      {
        init_steps (_total_parts);
        set_step (total);
      }
    }
  }

  Encoder * d (_encoder);
  _encoder = 0;
  update_work ();
  check_in (d);
}

TaskUpload :: ~TaskUpload ()
{
  // ensure our on_worker_done() doesn't get called after we're dead
  if (_encoder)
      _encoder->cancel_silently();
}
