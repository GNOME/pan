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

//  std::string get_basename(const char* f)
//  {
//    char buf[4096];
//    char * freeme = g_path_get_basename(f);
//    snprintf (buf, sizeof(buf), _("%s"), freeme);
//    g_free(freeme);
//    return buf;
//  }
}


/***
****
***/

TaskUpload :: TaskUpload (const std::string         & filename,
                          const Quark               & server,
                          EncodeCache               & cache,
                          quarks_t                  & groups,
                          std::string                 subject,
                          std::string                 author,
                          Article                   & article,
                          needed_t                  * imported,
                          Progress::Listener        * listener,
                          const TaskUpload::EncodeMode  enc):
  Task ("UPLOAD", get_description(filename.c_str())),
  _filename(filename),
  _basename (g_path_get_basename(filename.c_str())),
  _server(server),
  _cache(cache),
  _groups(groups),
  _subject (subject),
  _author(author),
  _encoder(0),
  _encoder_has_run (false),
  _encode_mode(enc),
  _lines_per_file(4000)
{
  if (listener != 0)
    add_listener (listener);

  needed_t& tmp = *imported;
  if (imported)
    foreach (needed_t, tmp, nit)
      _needed.insert(*nit);

  struct stat sb;
  stat(filename.c_str(),&sb);
  _bytes = sb.st_size;

  build_needed_tasks(imported);
  _cache.reserve(_article.get_part_mids());
  update_work ();
}

void
TaskUpload :: build_needed_tasks(bool imported)
{

  char buf[4096];
  char buf2[4096];

  _total_parts = (int) (((long)get_byte_count() + (4000*128-1)) / (4000*128));
  int cnt(1);

  quarks_t groups;
  foreach_const (Xref, _article.xref, it)
    groups.insert (it->group);

  for (int i=1; i<=_total_parts; ++i)
  {
    if (imported)
    {
    needed_t::iterator it = _needed.find(cnt);
    if (it == _needed.end())
      continue;
    }

    g_snprintf(buf,sizeof(buf),"%s.%d", _filename.c_str(), i);
    _article.add_part(i, StringView(buf), 0);
  }

  for (Article::part_iterator i(_article.pbegin()), e(_article.pend()); i!=e; ++i, ++cnt)
  {
    const std::string mid (i.mid ());

    TaskUpload::Needed n;
    n.message_id = mid;
    n.bytes = i.bytes();
    n.partno = cnt;

    foreach_const (quarks_t, groups, git)
      n.xref.insert (_server, *git, mid==_article.message_id.to_string()
                     ? _article.xref.find_number(_server,*git) : 0);

    std::cerr<<"needed insert "<<cnt<<std::endl;
    _needed.insert(std::pair<int,Needed>(cnt,n));
  }
  _needed_parts = cnt;
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

  if (!_encoder && !_encoder_has_run)
  {
    _state.set_need_encoder();
  } else if(working)
  {
    _state.set_working();
  } else if (_encoder_has_run && !_needed.empty())
  {
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
    nntp->post(StringView(data), this);
    update_work ();
  }
}

/***
****
***/

void
TaskUpload :: on_nntp_line (NNTP * nntp,
                              const StringView & line_in)
{}

void
TaskUpload :: on_nntp_done (NNTP * nntp,
                             Health health,
                             const StringView & response)
{

  char buf[4096];
  Log::Entry tmp;

  needed_t::iterator it;
  for (it=_needed.begin(); it!=_needed.end(); ++it)
    if (it->second.nntp == nntp)
      break;

  bool post_ok(false);
  switch (health)
  {
    case OK:
      // save to cache
      _needed.erase (it);
      post_ok = true;
      increment_step(1);
      break;
    case ERR_NETWORK:
      //reset
      it->second.nntp = 0;
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
      Log :: add_err_va ( _("Posting of File %s (Part %d of %d) failed: %s"),
                 _basename.c_str(), it->second.partno, _total_parts, response.str);
      break;
    case ARTICLE_POSTED_OK:
      tmp.severity = Log :: PAN_SEVERITY_INFO;
      if (post_ok && !_needed.empty())
      {
        g_snprintf(buf,sizeof(buf), _("Posting of file %s (Part %d of %d) succesful: %s"),
                   _basename.c_str(), it->second.partno, _total_parts, response.str);
        tmp.message = buf;
        _logfile.push_back(tmp);
        std::cerr<<LINE_ID<<" "<<_logfile.size()<<std::endl;
      } else if (post_ok && _needed.empty())
      {
        g_snprintf(buf,sizeof(buf), _("Posting of file %s (Part %d of %d) succesful: %s"),
                   _basename.c_str(), it->second.partno, _total_parts, response.str);
        tmp.message = buf;
        _logfile.push_back(tmp);
        g_snprintf(buf,sizeof(buf), _("Posting of file %s succesful: %s"),
                   _basename.c_str(), response.str);
        tmp.message = buf;
        _logfile.push_back(tmp);
        Log::add_entry_list (tmp, _logfile);
        std::cerr<<LINE_ID<<" "<<_logfile.size()<<std::endl;
      } else
      {
        Log :: add_err_va (_("Posting of file %s not successful: Check the popup log!"),
                   _basename.c_str(), response.str);
        std::cerr<<LINE_ID<<" "<<_logfile.size()<<std::endl;
      }
      break;
    case TOO_MANY_CONNECTIONS:
      // lockout for 120 secs, but try
      _state.set_need_nntp(nntp->_server);
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
  // build group name
  std::string groups;
  quarks_t::iterator it = _groups.begin();
  int i(0);
  for (; it != _groups.end(); it, ++it, ++i)
  {
    if (i<_groups.size()&& i>0 && _groups.size()>1) groups += ",";
    groups += (*it).to_string();
  }
  const Article::mid_sequence_t mids (_article.get_part_mids());
  foreach_const (Article::mid_sequence_t, mids, it)
    _cache.add(*it) ? std::cerr<<"fp valid!\n" :std::cerr<<"fp invalid!\n";
  _encoder->enqueue (this, mids, &_cache, _filename, _basename,
                     groups, _subject, _author, YENC);
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

    {
      std::cerr<<_encoder->parts<<" "<<_total_parts<<std::endl;
      set_step (0);
      init_steps(_needed_parts);
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
}
