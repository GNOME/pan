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
#include <pan/data/encode-cache.h>
#include "encoder.h"
#include "task-upload.h"
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

  std::string get_basename(const char* f)
  {
    char buf[4096];
    char * freeme = g_path_get_basename(f);
    snprintf (buf, sizeof(buf), _("%s"), freeme);
    g_free(freeme);
    return buf;
  }
}

/***
****
***/

TaskUpload :: TaskUpload (const std::string         & filename,
                          const Quark               & server,
                          EncodeCache               & cache,
                          Article                     article,
                          UploadInfo                  format,
                          GMimeMessage *              msg,
                          Progress::Listener        * listener,
                          const TaskUpload::EncodeMode  enc):
  Task ("UPLOAD", get_description(filename.c_str())),
  _filename(filename),
  _basename (get_basename(filename.c_str())),
  _server(server),
  _cache(cache),
  _article(article),
  _subject (article.subject.to_string()),
  _author(article.author.to_string()),
  _encoder(0),
  _encoder_has_run (false),
  _encode_mode(enc),
  _all_bytes(0),
  _format(format),
  _lpf(format.lpf),
  _queue_pos(0),
  _msg (msg)
{

  struct stat sb;
  stat(filename.c_str(),&sb);
  _bytes = sb.st_size;
  update_work ();
}

void
TaskUpload :: build_needed_tasks()
{

  foreach (needed_t, _needed, it)
  {
    _mids.push_back(Quark(it->second.message_id));
    _cache.add(Quark(it->second.message_id));
  }
  _cache.reserve(_mids);

  _needed_parts = _needed.size();
  _total_parts = _needed_parts;

   /* build new master subject */
  char sub[2048];
  g_snprintf(sub,2048,"%s - \"%s\" - (%03d/%03d)", _subject.c_str(), _basename.c_str(), 1, _total_parts);
  _master_subject = sub;

}

void
TaskUpload :: update_work (NNTP* checkin_pending)
{

  int working(0);
  foreach (needed_t, _needed, nit)
  {
    Needed& n (nit->second);
    if (n.nntp && n.nntp!=checkin_pending)
      ++working;
  }

  /* only need encode if mode is NOT plain */
  if (!_encoder && !_encoder_has_run && _msg)
  {
    _state.set_need_encoder();
  }
  else if(working)
  {
    _state.set_working();
  }
  else if ((_encoder_has_run && !_needed.empty()))
  {
    _state.set_need_nntp(_server);
  }
  else if (_needed.empty())
  {
    _state.set_completed();
    set_finished(_queue_pos);
  }
}

/***
****
***/

void
TaskUpload :: prepend_headers(GMimeMessage* msg, TaskUpload::Needed * n, std::string& d)
{
    std::stringstream out;

    //add headers
    if (!n->mid.empty()) pan_g_mime_message_set_message_id (msg, n->mid.c_str());

    //modify subject
    char buf[2048];
    g_snprintf(buf, sizeof(buf), "%s - \"%s\" - (%03d/%03d)", _subject.c_str(), _basename.c_str(), n->partno, _total_parts);
    g_mime_message_set_subject (msg, buf);

    //extract body
    gboolean unused;
    char * body (g_mime_object_to_string ((GMimeObject *) msg));
    out<< body<<"\r\n";
    out<<d;
    d = out.str();
}

void
TaskUpload :: use_nntp (NNTP * nntp)
{

  Needed * needed (0);
  foreach (needed_t, _needed, nit)
  {
      if (nit->second.nntp==0)
      {
        needed = &nit->second;
        break;
      }
  }

  if (!needed)
  {
    update_work (nntp);
    check_in (nntp, OK);
  }
  else
  {
    needed->nntp = nntp;

    set_status_va (_("Uploading %s - Part %d of %d"), _basename.c_str(), needed->partno, _total_parts);

    std::string data;
    _cache.get_data(data,needed->message_id.c_str());
    prepend_headers(_msg,needed, data);
    /* update cache file */
//    _cache.update_file (data,needed->message_id.c_str());
    nntp->post(StringView(data), this);
    update_work ();
  }
}

/***
****
***/

void
TaskUpload :: on_nntp_line (NNTP * nntp,
                              const StringView & line_in) {}

void
TaskUpload :: on_nntp_done (NNTP * nntp,
                             Health health,
                             const StringView & response)
{
  char buf[4096];
  Log::Entry tmp;
  tmp.date = time(NULL);
  tmp.is_child = true;

  needed_t::iterator it;
  for (it=_needed.begin(); it!=_needed.end(); ++it)
    if (it->second.nntp == nntp)
      break;

  bool post_ok(false);
  switch (health)
  {
    case OK:
      increment_step(it->second.bytes);
      _needed.erase (it);
      post_ok = true;
      break;
    case ERR_NETWORK:
      it->second.reset();
      goto _end;
    case ERR_COMMAND:
      _needed.erase (it);
      break;
  }

  switch (atoi(response.str))
  {
    case NO_POSTING:
      Log :: add_err_va (_("Posting of File %s (Part %d of %d) failed: No Posts allowed by server."),
                 _basename.c_str(), it->second.partno, _total_parts);
      this->stop();
      break;
    case POSTING_FAILED:
      if (health != OK)     // if we got a dupe, the health is OK, so skip that
      {
        tmp.severity = Log :: PAN_SEVERITY_ERROR;
        g_snprintf(buf,sizeof(buf), _("Posting of File %s (Part %d of %d) failed: %s"),
                   _basename.c_str(), it->second.partno, _total_parts, response.str);
        tmp.message = buf;
        _logfile.push_front(tmp);
      }
      break;
    case ARTICLE_POSTED_OK:
      tmp.severity = Log :: PAN_SEVERITY_INFO;
      if (post_ok && !_needed.empty())
      {
        g_snprintf(buf,sizeof(buf), _("Posting of file %s (Part %d of %d) succesful: %s"),
                   _basename.c_str(), it->second.partno, _total_parts, response.str);
        tmp.message = buf;
        _logfile.push_front(tmp);
      }
      else if (post_ok && _needed.empty())
      {
        g_snprintf(buf,sizeof(buf), _("Posting of file %s (Part %d of %d) succesful: %s"),
                   _basename.c_str(), it->second.partno, _total_parts, response.str);
        tmp.message = buf;
        _logfile.push_front(tmp);

        /* get error state for the whole upload: if one part failed, set global status to error */
        bool error(false);
        foreach_const (std::deque<Log::Entry>, _logfile, it)
          if (it->severity  == Log :: PAN_SEVERITY_ERROR) error = true;
        if (!error)
          g_snprintf(buf,sizeof(buf), _("Posting of file %s succesful: %s"),
                   _basename.c_str(), response.str);
        else
        {
          g_snprintf(buf,sizeof(buf), _("Posting of file %s not completely successful: Check the log (right-click on list item) !"),
                 _basename.c_str(), response.str);
          tmp.severity = Log :: PAN_SEVERITY_ERROR;
        }
        tmp.message = buf;
        Log::add_entry_list (tmp, _logfile);
        _logfile.clear();
      }

    case TOO_MANY_CONNECTIONS:
      // lockout for 120 secs, but try
      _state.set_need_nntp(nntp->_server);
      break;
    default:
      this->stop();
      Log::add_entry_list (tmp, _logfile);
      _logfile.clear();
      Log :: add_err_va (_("Posting of file %s not successful: Check the log (right-click on list item) !"),
                 _basename.c_str(), response.str);
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
    bytes += (unsigned long)it->second.bytes;
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

    ///TODO support other encode modes by choice of user
  _encoder->enqueue (this, &_cache, &_article, _filename, _basename, _master_subject, _lpf, _encode_mode);
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

    if (!_encoder->log_errors.empty()) {
        _needed.clear(); //update_work will then set the status to complete
    }

    if (!_encoder->log_severe.empty())
      _state.set_health (ERR_LOCAL);
    else
    {
      set_step (0);
      init_steps(_all_bytes);
      _encoder_has_run = true;
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

  g_object_unref (G_OBJECT(_msg));
  _cache.release(_mids);
  _cache.resize();
}
