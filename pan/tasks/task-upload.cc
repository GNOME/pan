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
  std::string get_description (const char* name, bool enc)
  {
    char buf[1024];
    if (!enc)
      snprintf (buf, sizeof(buf), _("Uploading %s"), name);
    else
      snprintf (buf, sizeof(buf), _("Encoding %s"), name);
    return std::string (buf);
  }
}

/***
****
***/

TaskUpload :: TaskUpload ( const FileQueue::FileData & file_data,
                           const Quark               & server,
                           std::string                 groups,
                           std::string                 subject,
                           std::string                 author,
                           Progress::Listener        * listener,
                           const TaskUpload::EncodeMode  enc):
  Task ("UPLOAD", get_description(file_data.filename.c_str(), false)),
  _file_data(file_data),
  _basename (file_data.basename),
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

  update_work ();
}

void
TaskUpload :: update_work (void)
{
  if (!_encoder && !_encoder_has_run)
  {
    _state.set_need_encoder();
  } else if (_encoder_has_run && !_needed.empty())
  {
    set_status_va (_("Uploading %s"), _basename.c_str());
    _state.set_need_nntp(_server);
  } else if (_needed.empty())
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
    Needed cur;
      if (!_needed.empty()) {
        cur = *(_needed.begin());
      } else {
        update_work();
        return;
      }

    std::stringstream tmp;
    std::ifstream in(cur.filename.c_str(), std::ifstream::in);
    while (in.good())
      tmp << (char) in.get();
    in.close();
    nntp->post(StringView(tmp.str()), this);
    update_work ();
}

/***
****
***/

void
TaskUpload :: on_nntp_line  (NNTP               * nntp,
                              const StringView   & line_in)
{

std::cerr<<"line "<<line_in<<std::cerr;

}

void
TaskUpload :: on_nntp_done  (NNTP             * nntp,
                             Health             health,
                             const StringView & response)
{

//  switch (atoi(response.str))
//  {
//    case NO_POSTING:
//      Log :: add_err(_("Posting failed: No Posts allowed by server."));
//      this->stop();
//      break;
//    case POSTING_FAILED:
//      Log :: add_err_va (_("Posting failed: %s"), response.str);
//      break;
//    case ARTICLE_POSTED_OK:
      if (_needed.empty())
        Log :: add_info_va(_("Posting of file %s succesful: %s"),
                           _file_data.basename.c_str(), response.str);
      else
        _needed.pop_front();
//      break;
//    case TOO_MANY_CONNECTIONS: //todo
//    break;
//  }

//  switch (health)
//  {
//    case OK:
      check_in (nntp, health);
      increment_step(1);
//      break;

//    case ERR_NETWORK:
//      _state.set_need_nntp(nntp->_server);
//      break;
//  }
  update_work();
}

/***
****
***/

//todo
unsigned long
TaskUpload :: get_bytes_remaining () const
{
  unsigned long bytes (0);
  foreach_const (needed_t, _needed, it)
    bytes += it->bytes;
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

  std::cerr<<"enqueue: "<<_groups<<" "<<_subject<<" "<<_author<<std::endl;
  _encoder->enqueue (this, _file_data, _groups, _subject, _author, YENC);
  debug ("encoder thread was free, enqueued work");
}

void
TaskUpload :: stop ()
{
  if (_encoder)
      _encoder->cancel();
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


    if (!_encoder->log_errors.empty())
      set_error (_encoder->log_errors.front());

    if (!_encoder->log_severe.empty())
      _state.set_health (ERR_LOCAL);
    else {
      // get parts number from encoder
      _parts = _encoder->parts;
      set_step (100);
      _encoder_has_run = true;
      /*enqueue all parts into the global needed_t list.
        update_work will then assign a pointer to the begin
        which will be used for an nntp upload.
        on nntp_done, the list is decreased by one member
       */
      static Needed n;
      char buf[2048];
      struct stat sb;

      for (int i=1;i<=_parts;i++)
      {
        n.partno = i;
        g_snprintf(buf,sizeof(buf),"%s/%s.%d",
                   file::get_uulib_path().c_str(),
                   _basename.c_str(), i);
        n.filename = buf;
        stat(buf, &sb);
        n.bytes = sb.st_size;
        _needed.push_back (n);
      }
      init_steps (_parts);
      set_step (0);
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
