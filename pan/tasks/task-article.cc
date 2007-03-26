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
extern "C" {
#include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/data/article-cache.h>
#include "decoder.h"
#include "task-article.h"

using namespace pan;

/***
****
***/

namespace
{
  std::string get_description (const Article& article, bool save)
  {
    std::string stripped;
    mime::remove_multipart_from_subject (article.subject.c_str(), stripped);

    char buf[1024];
    if (save)
      snprintf (buf, sizeof(buf), _("Saving %s"), stripped.c_str());
    else
      snprintf (buf, sizeof(buf), _("Reading %s"), stripped.c_str());
    return std::string (buf);
  }
}

TaskArticle :: TaskArticle (const ServerRank          & server_rank,
                            const GroupServer         & group_server,
                            const Article             & article,
                            ArticleCache              & cache,
                            ArticleRead               & read,
                            Progress::Listener        * listener,
                            SaveMode                    save_mode,
                            const Quark               & save_path):
  Task (save_path.empty() ? "BODIES" : "SAVE", get_description (article, !save_path.empty())),
  _save_path (save_path),
  _server_rank (server_rank),
  _cache (cache),
  _read (read),
  _article (article),
  _time_posted (article.time_posted),
  _save_mode (save_mode),
  _decoder(0),
  _decoder_has_run (false)
{
  cache.reserve (article.get_part_mids());

  if (listener != 0)
    add_listener (listener);

  // build a list of all the parts we need to download.
  // also calculate need_bytes and all_bytes for our Progress status.

  quarks_t groups;
  foreach_const (Xref, article.xref, it)
    groups.insert (it->group);
  quarks_t servers;
  foreach_const (quarks_t, groups, it) {
    quarks_t tmp;
    group_server.group_get_servers (*it, tmp);
    servers.insert (tmp.begin(), tmp.end());
  }

  unsigned long need_bytes(0), all_bytes(0);
  foreach_const (Article::parts_t, article.parts, it)
  {
    all_bytes += it->bytes;

    const std::string mid (it->get_message_id (article.message_id));
    if (!it->empty() && !cache.contains (mid))
    {
      need_bytes += it->bytes;

      Needed n;
      n.part = *it;

      // if we can keep the article-number from the main xref, do so.
      // otherwise plug in `0' as a null article-number and we'll use
      // `ARTICLE message-id' instead when talking to the server.
      foreach_const (quarks_t, servers, sit)
        foreach_const (quarks_t, groups, git)
          n.xref.insert (*sit, *git, mid==article.message_id.to_string() ? article.xref.find_number(*sit,*git) : 0);

      _needed.push_back (n);
    }
  }

  // initialize our progress status...
  init_steps (all_bytes);
  set_step (all_bytes - need_bytes);
  if (save_path.empty())
    set_status (article.subject.c_str());
  else
    set_status_va (_("Saving %s"), article.subject.c_str());

  update_work ();
}

TaskArticle :: ~TaskArticle ()
{
  
  if (_decoder) {
    // NB: _decoder is active here if quitting pan .. so be sure to 
    // be careful and notify _decoder that we mean business and are QUIT..
    // this prevents the _decoder from calling any more methods on us (WorkerPool::Worker::Listener *)
    _decoder->gracelessly_quit();
  }
  
  _cache.release (_article.get_part_mids());
}

void
TaskArticle :: update_work ()
{
  // which servers could we use right now?
  int working (0);
  quarks_t servers;
  foreach (needed_t, _needed, nit) {
    Needed& n (*nit);
    if (n.nntp)
      ++working;
    else {
      quarks_t tmpservers;
      while (!n.xref.empty() && tmpservers.empty()) {
        foreach_const (Xref, n.xref, xit)
          if (_server_rank.get_server_rank(xit->server) <= n.rank)
            tmpservers.insert (xit->server);
        if (tmpservers.empty())
          ++n.rank;
      }
      servers.insert (tmpservers.begin(), tmpservers.end());
    }
  }

  if (!servers.empty())
    _state.set_need_nntp (servers);
  else if (working)
    _state.set_working ();
  else if (_save_mode && !_decoder && !_decoder_has_run) {
    _state.set_need_decoder ();
    set_step(0);
  } else if (!_save_mode || _decoder_has_run) {
    _state.set_completed();
    set_finished (OK);
  } else assert(0 && "hm, missed a state.");
}

unsigned long
TaskArticle :: get_bytes_remaining () const
{
  unsigned long bytes (0);
  foreach_const (needed_t, _needed, it) // parts not fetched yet...
    bytes += (it->part.bytes - it->buf.size());
  return bytes;
}

/***
****
***/

void
TaskArticle :: use_nntp (NNTP * nntp)
{
  // find which part, if any, can use this nntp
  Needed * needed (0);
  for (needed_t::iterator it(_needed.begin()), end(_needed.end()); !needed && it!=end; ++it)
    if (it->nntp==0 && it->xref.has_server(nntp->_server) && (it->rank <= _server_rank.get_server_rank (nntp->_server)))
      needed = &*it;

  if (!needed)
  {
    // couldn't find any work for this nntp...
    // maybe our needs-work list is out of date, so call update_work()
    // before giving the nntp back.
    update_work ();
    check_in (nntp, OK);
  }
  else
  {
    needed->nntp = nntp;
    needed->buf.clear ();

    Quark group;
    unsigned long number (0ul);
    needed->xref.find (nntp->_server, group, number);
    if (number)
      nntp->article (group, number, this);
    else
      nntp->article (group, needed->part.get_message_id(_article.message_id).c_str(), this);
    update_work ();
  }
}

/***
****
***/

void
TaskArticle :: on_nntp_line  (NNTP               * nntp,
                             const StringView   & line)
{
  // FIXME: ugh, this is called for _every line_...
  Needed * needed (0);
  foreach (needed_t, _needed, it) {
    if (it->nntp == nntp) {
      needed = &*it;
      break;
    }
  }
  assert (needed);

  Needed::buf_t& buf (needed->buf);
  buf.insert (buf.end(), line.begin(), line.end());
  buf.insert (buf.end(), '\n');
  increment_step (line.len);
}

void
TaskArticle :: on_nntp_done  (NNTP             * nntp,
                              Health             health,
                              const StringView & response)
{
  // find the Needed using this nntp...
  needed_t::iterator it;
  for (it=_needed.begin(); it!=_needed.end(); ++it)
    if (it->nntp == nntp)
      break;
  assert (it != _needed.end());

  //std::cerr << LINE_ID << ' ' << it->part.message_id << " from " << nntp->_server << ": " << (health==OK ? "yes" : "no") << std::endl;

  if (health == OK) { // if download succeeded, save it in the cache
    const StringView view (&it->buf.front(), it->buf.size());
    _cache.add (it->part.get_message_id(_article.message_id), view);
  }

  if (health == COMMAND_FAILED) // if server doesn't have that article...
    it->xref.remove_server (nntp->_server);

  // if we got it or still have other options, we're okay
  _state.set_health (health==COMMAND_FAILED && it->xref.empty() ? COMMAND_FAILED : OK);

  if (health==OK)
    _needed.erase (it);
  else {
    Needed::buf_t tmp;
    it->buf.swap (tmp); // deallocates the space...
    it->nntp = 0;
  }

  update_work ();
  check_in (nntp, health);
}

/***
****
***/

void
TaskArticle :: use_decoder (Decoder* decoder)
{
  if (_state._work != NEED_DECODER)
    check_in (decoder);

  _decoder = decoder;
  init_steps(100);
  _state.set_working();
  const Article::mid_sequence_t mids (_article.get_part_mids());
  const ArticleCache :: strings_t filenames (_cache.get_filenames (mids));
  _decoder->enqueue_work_in_thread (this, _decoder, _save_path, filenames, _save_mode);
  set_status_va (_("Decoding %s"), _article.subject.c_str());
  debug ("decoder thread was free, enqueued work");
}

void
TaskArticle :: stop ()
{
  if (_decoder)
      _decoder->cancel();
}

// called in the main thread by WorkerPool if the worker was cancelled.
void
TaskArticle :: on_work_cancelled (void *data)
{
  Decoder * d (_decoder);
  _decoder = 0;
  update_work ();
  check_in (d);
}

// called in the main thread by WorkerPool if the worker completed.
void
TaskArticle :: on_work_complete (void *data)
{
  assert(_decoder);
  if (!_decoder) return;

  // the decoder is done... catch up on all housekeeping
  // now that we're back in the main thread.

  foreach_const(std::list<std::string>, _decoder->log_errors, it)
    Log :: add_err(it->c_str());
  foreach_const(std::list<std::string>, _decoder->log_infos, it)
    Log :: add_info(it->c_str());

  if (_decoder->mark_read)
    _read.mark_read(_article);

  if (!_decoder->log_errors.empty())
    set_error (_decoder->log_errors.front());

  _state.set_completed();
  set_step (100);
  _decoder_has_run = true;

  Decoder * d (_decoder);
  _decoder = 0;
  update_work ();
  check_in (d);
}
