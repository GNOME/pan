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
                           GMimeMessage              * msg,
                           Progress::Listener        * listener,
                           const TaskUpload::EncodeMode  enc):
  Task ("UPLOAD", get_description(file_data.filename, true)),
  _file_data(file_data),
  _basename(g_path_get_basename(file_data.filename)),
  _server(server),
  _msg(msg),
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
    std::cout<<"need "<<_needed.size()<<std::endl;
    mut.lock();
      _cur = *_needed.begin();
      if (!_needed.empty()) _needed.pop_front();
    mut.unlock();
    _state.set_need_nntp(_server);
    set_status_va (_("Uploading %s"), _file_data.basename);
  } else if (_needed.empty())
  {
    _state.set_completed();
    set_finished(OK);
  }
}

/***
****
***/

std::string
TaskUpload :: generate_yenc_headers(const Needed& n)
{
  std::stringstream res;
  const int bufsz = 2048;
  char buf[bufsz];
  //modify msg to our needs
  const char* subject = g_mime_object_get_header ((GMimeObject*)_msg, "Subject");
  g_snprintf(buf,bufsz, "%s -- \"%s\" (%d/%d) YEnc",
             subject, _file_data.basename, //_file_data.part_in_queue, _file_data.size(),
             n.partno, _parts);
  g_mime_object_set_header((GMimeObject*)_msg,"Subject", buf);

  //append msg to result
  res << g_mime_object_to_string((GMimeObject *)_msg);

  //append yenc data to result
  res<<"\r\n";

  std::ifstream in(const_cast<char*>(n.filename.c_str()),  std::ifstream::in);
  std::string line;
  if (in.good())
    std::getline(in, line);
  int filesize = __yenc_extract_tag_val_int_base( line.c_str(), " size=", 0 );
  g_snprintf(buf,bufsz,"=ybegin line=128 size=%s name=%s\r\n",
             filesize, _basename);
  res<<buf;
  while (in.good())
    res << (char) in.get();
  res<<"\r\n.\r\n";
  return res.str();
}

void
TaskUpload :: use_nntp (NNTP * nntp)
{
    std::cerr<<"use nntp\n";
    if (_needed.empty())
      update_work();

    std::string res(generate_yenc_headers(_cur));
    nntp->post(StringView(res), this);
    update_work ();
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
  std::cerr<<"nntp done\n";
  check_in (nntp, health);
  increment_step(1);
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
  foreach_const (needed_t, _needed, it) // parts not fetched yet...
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
  _encoder->enqueue (this, _file_data, YENC);
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
      /*enqueue all parts into needed_t list.
        update_work will then assign a pointer to the begin
        which will be used for an nntp upload.
        on nntp_done, the list is decreased by one member
       */
      static Needed n;
      const int bufsz(2048);
      char buf[bufsz];
      struct stat sb;

      for (int i=1;i<=_parts;i++)
      {
        n.partno = i;
        g_snprintf(buf,bufsz,"%s/%s.%d",
                   file::get_uulib_path().c_str(),
                   _file_data.basename, i);
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
