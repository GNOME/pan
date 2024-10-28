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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>
#include <cassert>
#include <cstdint>
#include <glib/gi18n.h>
#include <log4cxx/logger.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/utf8-utils.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/text-massager.h>
#include <pan/data/article-cache.h>
#include "decoder.h"
#include "pan/general/article_number.h"
#include "pan/general/log4cxx.h"
#include "task-article.h"

using namespace pan;

/***
****
***/

namespace
{
log4cxx::LoggerPtr logger = getLogger("task-article");

  std::string get_description (const Article& article, bool save)
  {
    std::string stripped;
    mime::remove_multipart_from_subject (article.get_subject().c_str(), stripped);

    const char* str = stripped.c_str();
    iconv_t c = iconv_open("UTF-8","UTF-8");
    char * res = __g_mime_iconv_strdup(c, str);
    iconv_close(c);

    char buf[1024];
    if (save)
      snprintf (buf, sizeof(buf), _("Saving %s"), res);
    else
      snprintf (buf, sizeof(buf), _("Reading %s"), res);

    g_free(res);

    return std::string (buf);
  }

  std::string get_groups_str(const Article& a)
  {
    SQLite::Statement q(pan_db, R"SQL(
      select group_concat(g.name, ", ")
      from `group` as g
      join article_group as ag on ag.group_id == g.id
      join article as a on ag.article_id == a.id
      where a.message_id = ?
      order by g.name asc
    )SQL");

    q.bind(1,a.message_id);

    std::string r;
    while (q.executeStep()) {
      r = q.getColumn(0).getText();
    }

    return r;
  }
}

TaskArticle :: TaskArticle (const ServerRank          & server_rank,
                            const GroupServer         & group_server,
                            const Article             & article,
                            ArticleCache              & cache,
                            ArticleRead               & read,
                            const ArticleActionType& mark_read_action,
                            Progress::Listener        * listener,
                            SaveMode                    save_mode,
                            const Quark               & save_path,
                            const char                * filename,
                            const SaveOptions         & options):
  Task (save_path.empty() ? "BODIES" : "SAVE", get_description (article, !save_path.empty())),
  _save_path (expand_attachment_headers(save_path, article)),
  _server_rank (server_rank),
  _cache (cache),
  _read (read),
  _article (article),
  _time_posted (article.get_time_posted()),
  _attachment(filename),
  _mark_read_action (mark_read_action),
  _save_mode (save_mode),
  _decoder(nullptr),
  _decoder_has_run (false),
  _groups(get_groups_str(article)),
  _options(options),
  _paused(false)
{
  LOG4CXX_TRACE(logger, "Set download task for article " << article.get_subject());

  cache.reserve (article.get_part_mids());

  if (listener != nullptr)
    add_listener (listener);

  // build a list of all the parts we need to download.
  // also calculate need_bytes and all_bytes for our Progress status.
  SQLite::Statement q(pan_db, R"SQL(
    select  p.size, part_message_id, s.pan_id, g.name, number, part_number from article_part as p
      join article as a on p.article_id == a.id
  	join article_group as ag on ag.article_id == a.id
  	join `group` as g on ag.group_id = g.id
  	join article_xref as xr on xr.article_group_id == ag.id
  	join server as s on xr.server_id = s.id
    where a.message_id = ?
    order by cast(part_number as integer)
  )SQL");
  q.bindNoCopy(1,article.message_id.c_str());

  unsigned long need_bytes(0), all_bytes(0);
  while (q.executeStep()) {
    int psize(q.getColumn(0));
    all_bytes += psize;
    const std::string part_mid (q.getColumn(1).getText());
    if (cache.contains (part_mid))
      continue;

    need_bytes += psize;
    Needed n;
    n.message_id = part_mid;
    n.bytes = psize;
    // if we can keep the article-number from the main xref, do so.
    // otherwise plug in `0' as a null article-number and we'll use
    // `ARTICLE message-id' instead when talking to the server.
    Quark server_pan_id(q.getColumn(2).getText());
    Quark group_name(q.getColumn(3).getText());
    Article_Number p_nb (q.getColumn(4).getInt());
    int64_t part_nb (q.getColumn(5));
    LOG4CXX_TRACE(logger, "task: download part nb " << part_nb << " of article "  << article.message_id);
    n.xref.insert (server_pan_id, group_name,
                   part_mid==article.message_id.to_string() ? p_nb : static_cast<Article_Number>(0));
    _needed.push_back (n);
  }

  // initialize our progress status...
  init_steps (all_bytes);
  set_step (all_bytes - need_bytes);
  const Quark artsub( article.get_subject() );
  LOG4CXX_TRACE(logger, "saving article " << artsub.c_str());
  if (save_path.empty())
    set_status (artsub.to_view());
  else
    set_status_va (_("Saving %s"), artsub.c_str());

  update_work ();
}

TaskArticle :: ~TaskArticle ()
{
  // ensure our on_worker_done() doesn't get called after we're dead
  if (_decoder)
      _decoder->cancel_silently();

  _cache.release (_article.get_part_mids());
}

void
TaskArticle :: update_work (NNTP * checkin_pending)
{
  // which servers could we use right now?
  int working (0);
  quarks_t servers;
  foreach (needed_t, _needed, nit) {
    Needed& n (*nit);
    if (n.nntp && n.nntp!=checkin_pending)
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
    bytes += (it->bytes - it->buf.size());
  return bytes;
}

/***
****
***/

void
TaskArticle :: use_nntp (NNTP * nntp)
{
  // find which part, if any, can use this nntp
  Needed * needed (nullptr);
  for (needed_t::iterator it(_needed.begin()), end(_needed.end()); !needed && it!=end; ++it)
    if (it->nntp==nullptr && it->xref.has_server(nntp->_server) && (it->rank <= _server_rank.get_server_rank (nntp->_server)))
      needed = &*it;

  if (!needed)
  {
    // std::cerr << LINE_ID << " hmm, why did I ask for server " << nntp->_server
    //           << "?  I can't use it.  I'd better refresh my worklist." << std::endl;
    update_work (nntp);
    check_in (nntp, OK);
  }
  else
  {
    needed->nntp = nntp;
    needed->buf.clear ();

    Quark group;
    Article_Number number (0ul);
    needed->xref.find (nntp->_server, group, number);
    if (static_cast<uint64_t>(number) != 0)
      nntp->article (group, number, this);
    else
      nntp->article (group, needed->message_id.c_str(), this);
    update_work ();
  }
}

/***
****
***/

void
TaskArticle :: on_nntp_line  (NNTP               * nntp,
                              const StringView   & line_in)
{
  // FIXME: ugh, this is called for _every line_...
  Needed * needed (nullptr);
  foreach (needed_t, _needed, it) {
    if (it->nntp == nntp) {
      needed = &*it;
      break;
    }
  }
  assert (needed);

  // some multiline headers have an extra linefeed... see bug #393589
  StringView line (line_in);
  if (line.len && line.str[line.len-1] == '\n')
    line.truncate (line.len-1);

  Needed::buf_t& buf (needed->buf);
  buf.insert (buf.end(), line.begin(), line.end());
  buf.insert (buf.end(), '\n');
  increment_step (line.len);
}

void
TaskArticle :: on_nntp_done  (NNTP             * nntp,
                              Health             health,
                              const StringView & response UNUSED)
{
  // find the Needed using this nntp...
  needed_t::iterator it;
  for (it=_needed.begin(); it!=_needed.end(); ++it)
    if (it->nntp == nntp)
      break;
  assert (it != _needed.end());

  if (health == OK) { // if download succeeded, save it in the cache
    const StringView view (&it->buf.front(), it->buf.size());
    ArticleCache::CacheResponse res (_cache.add (it->message_id, view));
    if (ArticleCache::CACHE_OK != res.type)
      health = res.type == ArticleCache::CACHE_DISK_FULL ? ERR_NOSPACE : ERR_LOCAL;
    if (health == ERR_NOSPACE)
      _state.set_health (ERR_NOSPACE);
  }

  // std::cerr << LINE_ID << ' ' << it->message_id << " from " << nntp->_server << ": health " << health << std::endl;

  switch (health)
  {
    case OK: // if we got the article successfully...
      _needed.erase (it);
      break;

    case ERR_NETWORK: // if the network is bad...
    case ERR_NOSPACE: // if there's no space, try again, but pause the queue!
    case ERR_LOCAL: // ...or if we got it but couldn't save it
      it->reset ();
      break;

    case ERR_COMMAND: // if this one server doesn't have this part...
      it->xref.remove_server (nntp->_server);
      if (!it->xref.empty())
        it->reset ();
      else { // if none of our servers have this part, but keep going --
             // an incomplete file gives us more PAR2 blocks than a missing one.
        Log :: add_err_va (
          _("Article \"%s\" is incomplete -- the news server(s) don't have part %s"),
          _article.get_subject().c_str(),
          it->message_id.c_str());
        _needed.erase (it);
      }
      break;
  }

  update_work (nntp);
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
  ArticleCache :: strings_t filenames (_cache.get_filenames (mids));
  Quark subject(_article.get_subject());
  _decoder->enqueue (this, _save_path, filenames, _save_mode, _options, _attachment, subject);
  set_status_va (_("Decoding %s"), subject.c_str());
  LOG4CXX_DEBUG(logger,"decoder thread was free, enqueued decoding work for " << subject.c_str());
}

void
TaskArticle :: stop ()
{
  if (_decoder)
      _decoder->cancel();
}

// called in the main thread by WorkerPool
void
TaskArticle :: on_worker_done (bool cancelled)
{
  assert(_decoder);
  if (!_decoder) return;

  if (!cancelled)
  {
    // the decoder is done... catch up on all housekeeping
    // now that we're back in the main thread.

    foreach_const(Decoder::log_t, _decoder->log_severe, it)
    {
      Log :: add_err(it->c_str());
      verbose (it->c_str());
    }
    foreach_const(Decoder::log_t, _decoder->log_errors, it)
    {
      Log :: add_err(it->c_str());
      verbose (it->c_str());
    }
    foreach_const(Decoder::log_t, _decoder->log_infos, it)
    {
      Log :: add_info(it->c_str());
      verbose (it->c_str());
    }

    // marks read if either there was no filter action involved or
    // the user chose to mark read after the action
    const bool act_on_action (_mark_read_action == ACTION_TRUE);
    const bool no_action (_mark_read_action == NO_ACTION);
    const bool always (_mark_read_action == ALWAYS_MARK);
    const bool never (_mark_read_action == NEVER_MARK);
    if (!never)
    {
      if (_decoder->mark_read && (no_action || act_on_action || always))
        _read.mark_read(_article);
    }

    if (!_decoder->log_errors.empty())
      set_error (_decoder->log_errors.front());

    _state.set_health(_decoder->health);

    if (!_decoder->log_severe.empty())
      _state.set_health (ERR_LOCAL);
    else {
      _state.set_completed();
      set_step (100);
      _decoder_has_run = true;
    }
  }

  Decoder * d (_decoder);
  _decoder = nullptr;
  update_work ();
  check_in (d);
}
