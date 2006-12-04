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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <ostream>
#include <sstream>
extern "C" {
  #define PROTOTYPES
  #include <uulib/uudeview.h>
  #include <glib/gi18n.h>
};
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/data/article-cache.h>
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
                            Task::Listener            * listener,
                            SaveMode                    save_mode,
                            const Quark               & save_path):
  Task (save_path.empty() ? "BODIES" : "SAVE", get_description (article, !save_path.empty())),
  _save_path (save_path),
  _server_rank (server_rank),
  _cache (cache),
  _read (read),
  _article (article),
  _time_posted (article.time_posted),
  _finished_proc_has_run (false),
  _save_mode (save_mode)
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
  else {
    _state.set_completed ();
    set_finished (OK);
  }

  if (_state._work == COMPLETED && !_finished_proc_has_run) {
    _finished_proc_has_run = true;
    on_finished ();
  }
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
    check_in (nntp, true);
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
  g_assert (it != _needed.end());

  //std::cerr << LINE_ID << ' ' << it->part.message_id << " from " << nntp->_server << ": " << (health==OK ? "yes" : "no") << std::endl;

  if (health == OK) { // if download succeeded, save it in the cache
    const StringView view (&it->buf.front(), it->buf.size());
    _cache.add (it->part.get_message_id(_article.message_id), view);
    ++_stats[nntp->_server];
  }

  if (health == FAIL) // if server doesn't have that article...
    it->xref.remove_server (nntp->_server);

  // if we got it or still have other options, we're okay
  _state.set_health (health==FAIL && it->xref.empty() ? FAIL : OK);

  if (health==OK)
    _needed.erase (it);
  else {
    Needed::buf_t tmp;
    it->buf.swap (tmp); // deallocates the space...
    it->nntp = 0;
  }

  update_work ();
  check_in (nntp, health!=RETRY);
}

namespace
{
  void uu_log (void* unused, char* message, int severity)
  {
    char * pch (g_locale_to_utf8 (message, -1, 0, 0, 0));

    if (severity == UUMSG_PANIC || severity==UUMSG_FATAL || severity==UUMSG_ERROR)
      Log :: add_err (pch ? pch : message);
    else if (severity == UUMSG_WARNING || severity==UUMSG_NOTE)
      Log :: add_info (pch ? pch : message);

    g_free (pch);
  }
}

void
TaskArticle :: on_finished ()
{
  const Article::mid_sequence_t mids (_article.get_part_mids());
  const ArticleCache :: strings_t filenames (_cache.get_filenames (mids));

  if (_save_mode & RAW)
  {
    foreach_const (ArticleCache::strings_t, filenames, it)
    {
      gchar * contents (0);
      gsize length (0);
      if (g_file_get_contents (it->c_str(), &contents, &length, NULL) && length>0)
      {
        file :: ensure_dir_exists (_save_path.c_str());
        gchar * basename (g_path_get_basename (it->c_str()));
        gchar * filename (g_build_filename (_save_path.c_str(), basename, NULL));
        FILE * fp = fopen (filename, "w+");
        if (!fp)
          Log::add_err_va (_("Couldn't save file \"%s\": %s"), filename, file::pan_strerror(errno));
        else {
          fwrite (contents, 1, (size_t)length, fp);
          fclose (fp);
        }
        g_free (filename);
        g_free (basename);
      }
      g_free (contents);
    }
  }

  if (_save_mode & DECODE)
  {
    // decode
    int res;
    if (((res = UUInitialize())) != UURET_OK)
      Log::add_err (_("Error initializing uulib"));
    else
    {
      UUSetMsgCallback (NULL, uu_log);
      UUSetOption (UUOPT_DESPERATE, 1, NULL); // keep incompletes -- they're still useful to par2

      int i (0);
      foreach_const (ArticleCache::strings_t, filenames, it) {
        if ((res = UULoadFileWithPartNo (const_cast<char*>(it->c_str()), 0, 0, ++i)) != UURET_OK)
          Log::add_err_va (_("Couldn't load \"%s\": %s"), it->c_str(),
            (res==UURET_IOERR) ?  file::pan_strerror (UUGetOption (UUOPT_ERRNO, NULL, NULL, 0)) : UUstrerror(res));
      }

      i = 0;
      uulist * item;
      while ((item = UUGetFileListItem (i++)))
      {
        // make sure the directory exists...
        if (!_save_path.empty())
          file :: ensure_dir_exists (_save_path.c_str());

        // find a unique filename...
        char * fname (0);
        for (int i=0; ; ++i) {
          std::string basename ((item->filename && *item->filename)
                                ? item->filename
                                : "pan-saved-file");
          if (i) {
            char buf[32];
            g_snprintf (buf, sizeof(buf), "_copy_%d", i+1); // we don't want "_copy_1"
            // try to preserve any extension
            std::string::size_type dotwhere = basename.find_last_of(".");
            if (dotwhere != basename.npos) {// if we found a dot
          	  std::string bn (basename, 0, dotwhere); // everything before the last dot
          	  std::string sf (basename, dotwhere, basename.npos); // the rest
          	  // add in a substring to make it unique and enable things like "rm -f *_copy_*"
          	  basename = bn + buf + sf;
            }else{
          	  basename += buf;
            }
          }
          fname = _save_path.empty()
            ? g_strdup (basename.c_str())
            : g_build_filename (_save_path.c_str(), basename.c_str(), NULL);
          if (!file::file_exists (fname))
            break;
          g_free (fname);
        }

        // decode the file...
        if ((res = UUDecodeFile (item, fname)) == UURET_OK)
          Log::add_info_va (_("Saved \"%s\""), fname);
        else {
          const int the_errno (UUGetOption (UUOPT_ERRNO, NULL, NULL, 0));
          if (res==UURET_IOERR && the_errno==ENOSPC)
            Log::add_urgent_va (_("Error saving \"%s\":\n%s. %s"), fname, file::pan_strerror(the_errno), "ENOSPC");
          else
            Log::add_err_va (_("Error saving \"%s\":\n%s."),
                             fname,
                             res==UURET_IOERR ? file::pan_strerror(the_errno) : UUstrerror(res));
        }

        // cleanup
        g_free (fname);
      }

      _read.mark_read (_article);
    }
    UUCleanUp ();
  }
}

unsigned long
TaskArticle :: get_bytes_remaining () const
{
  unsigned long bytes (0);
  foreach_const (needed_t, _needed, it) // parts not fetched yet...
    bytes += (it->part.bytes - it->buf.size());
  return bytes;
}
