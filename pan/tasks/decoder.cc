/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2007 Calin Culianu <calin@ajvar.org>
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include <cerrno>
extern "C" {
#  define PROTOTYPES
#  include <uulib/uudeview.h>
#  include <glib/gi18n.h>
};
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/foreach.h>
#include "decoder.h"

using namespace pan;

Decoder :: Decoder (WorkerPool& pool):
  _worker_pool (pool),
  _gsourceid (-1)
{
}

Decoder :: ~Decoder()
{
  disable_progress_update();
}

// save article IN A WORKER THREAD to avoid network stalls
void
Decoder :: do_work()
{
  const int bufsz = 4096;
  char buf[bufsz];

  enable_progress_update();

  if (save_mode & TaskArticle::RAW)
  {
    int i = 0;
    foreach_const (strings_t, input_files, it)
    {
      if (was_cancelled()) break; // poll WorkerPool::Worker stop flag

      gchar * contents (0);
      gsize length (0);
      if (g_file_get_contents (it->c_str(), &contents, &length, NULL) && length>0)
      {
        file :: ensure_dir_exists (save_path.c_str());
        gchar * basename (g_path_get_basename (it->c_str()));
        gchar * filename (g_build_filename (save_path.c_str(), basename, NULL));
        FILE * fp = fopen (filename, "w+");

        mut.lock();
        current_file = filename;
        mut.unlock();

        if (!fp) {
          g_snprintf(buf, bufsz, _("Couldn't save file \"%s\": %s"), filename, file::pan_strerror(errno));
          log_errors.push_back (buf); // log error
        } else {
          fwrite (contents, 1, (size_t)length, fp);
          fclose (fp);
        }
        g_free (filename);
        g_free (basename);
      }
      g_free (contents);

      mut.lock();
      percent = ++i*100/input_files.size();
      mut.unlock();
    }
  }

  if (save_mode & TaskArticle::DECODE)
  {
    // decode
    int res;
    if (((res = UUInitialize())) != UURET_OK)
      log_errors.push_back(_("Error initializing uulib")); // log error
    else
    {
      UUSetMsgCallback (this, uu_log);
      UUSetOption (UUOPT_DESPERATE, 1, NULL); // keep incompletes; they're useful to par2

      int i (0);
      foreach_const (strings_t, input_files, it) {
        if (was_cancelled()) break;
        if ((res = UULoadFileWithPartNo (const_cast<char*>(it->c_str()), 0, 0, ++i)) != UURET_OK) {
          g_snprintf(buf, bufsz,
                   _("Error reading from %s: %s"),
                   it->c_str(),
                   (res==UURET_IOERR)
                   ?  file::pan_strerror (UUGetOption (UUOPT_ERRNO, NULL,
                                                       NULL, 0))
                   : UUstrerror(res));
          log_errors.push_back(buf); // log error
        }
        mut.lock();
        // phase I of progress..
        int tmp = percent = 10/input_files.size(); // update percentage progress member .. this goes up to 10% and the remaining 90% is calculated from the actually uuprogress given us by uulib
        mut.unlock();
        debug("uudecoder thread: first pass progress " << tmp << "%");
      }

      UUSetBusyCallback (this, uu_busy_poll, 500); // .5 secs busy poll?

      uulist * item;
      i = 0;
      while ((item = UUGetFileListItem (i++)))
      {
        if (was_cancelled()) break; // poll WorkerPool::Worker stop flag

        // make sure the directory exists...
        if (!save_path.empty())
          file :: ensure_dir_exists (save_path.c_str());

        // find a unique filename...
        char * fname (0);
        for (int i=0; ; ++i) {
          std::string basename ((item->filename && *item->filename)
                                ? item->filename
                                : "pan-saved-file");
          if (i) {
            g_snprintf (buf, bufsz, "_copy_%d", i+1); // we don't want "_copy_1"
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
          fname = save_path.empty()
            ? g_strdup (basename.c_str())
            : g_build_filename (save_path.c_str(), basename.c_str(), NULL);
          if (!file::file_exists (fname))
            break;
          g_free (fname);
        }

        // decode the file...
        if ((res = UUDecodeFile (item, fname)) == UURET_OK) {
          g_snprintf(buf, bufsz,_("Saved \"%s\""), fname);
          log_infos.push_back(buf); // log info
        } else if (res == UURET_NODATA) {
          // silently let this error by... user probably tried to
          // save attachements on a text-only post
        } else {
          const int the_errno (UUGetOption (UUOPT_ERRNO, NULL, NULL, 0));
          if (res==UURET_IOERR && the_errno==ENOSPC) {
            g_snprintf (buf, bufsz, _("Error saving \"%s\":\n%s. %s"), fname, file::pan_strerror(the_errno), "ENOSPC");
            log_errors.push_back(buf); // log this to the error log -- used to be log_urgents but we decided against an urgent list because it can popup too many dialogs?  bug #420618 discussion..
          } else {
            g_snprintf (buf, bufsz,_("Error saving \"%s\":\n%s."),
                             fname,
                             res==UURET_IOERR ? file::pan_strerror(the_errno) : UUstrerror(res));
            log_errors.push_back(buf); // log error
          }
        }

        // cleanup
        g_free (fname);

      }

      mark_read = true;
    }
    UUCleanUp ();
  }

  if (was_cancelled())
    debug("got notification to stop early, decode might not be finished..");
  disable_progress_update();
}

/* static */
void
Decoder :: uu_log (void* data, char* message, int severity)
{
    Decoder *self = reinterpret_cast<Decoder *>(data);
    char * pch (g_locale_to_utf8 (message, -1, 0, 0, 0));

    if (severity == UUMSG_PANIC || severity==UUMSG_FATAL || severity==UUMSG_ERROR)
      self->log_errors.push_back (pch ? pch : message);
    else if (severity == UUMSG_WARNING || severity==UUMSG_NOTE)
      self->log_infos.push_back (pch ? pch : message);

    g_free (pch);
}

/* static */
int
Decoder :: uu_busy_poll (void *data, uuprogress *p)
{
  Decoder *self = reinterpret_cast<Decoder *>(data);
  if (self->was_cancelled()) {
    debug("uudecoder thread: got stop request, aborting early");
    return 1;
  }

  debug("uudecoder thread: uuprogress is " << p->percent << " percent on " << p->fsize << "b file `" << p->curfile << "' part " << p->partno << " of " << p->numparts);

  assert(p->numparts);

  self->mut.lock();

  double pct = p->percent;
  if (p->numparts == 1) // we are in phase III of uudecode.. so pick up percent here by offsetting from 20.0% becasue Phase I and II each took 10%
    self->percent =  int( pct = (pct/100.0) * (100.0-20.0) + 20.0 ); // complicated way to update percentage -- this is because the uudecode progress is reset to 0 for each phase so we have to fudge its value
  else if(p->numparts) // phase II is here, it takes about 10% of total time (phase I was in the do_work function and it took also 10%)
    self->percent = int(pct = 10 + p->partno*10/p->numparts);
  else  { // something is wrong, this shouldn't be reached.. we have this here to guard against division by zero above
    self->percent = int (pct); // noop
    debug ("uudecoder thread got p->numparts == 0!  HELP!");
  }

  self->current_file = p->curfile;

  self->mut.unlock();
  
  debug("uudecoder thread: calculated percent is " << pct);

  return 0;
}

void
Decoder :: enqueue (TaskArticle                 * task,
                    const Quark                 & save_path,
                    const strings_t             & input_files,
                    const TaskArticle::SaveMode & save_mode)
{
  disable_progress_update ();

  this->task = task;
  this->save_path = save_path;
  this->input_files = input_files;
  this->save_mode = save_mode;

  mark_read = false;
  percent = 0;
  current_file.clear ();
  log_infos.clear();
  log_errors.clear();

  // gentlemen, start your saving...
  _worker_pool.push_work (this, task, false);
}

gboolean
Decoder :: progress_update_timer_func(gpointer decoder)
{
  Decoder *self = reinterpret_cast<Decoder *>(decoder);
  Task *task = self->task;
  if (!task || self->was_cancelled()) return false;

  self->mut.lock();
  int percent = self->percent;
  std::string f = self->current_file;
  self->mut.unlock();
  task->set_step(percent);
  task->set_status_va (_("Decoding %s"), f.c_str());
  debug("setting task progress to: " << percent << "% file: " << f);

  return true; // keep timer func running
}

void
Decoder :: enable_progress_update ()
{
  if (_gsourceid == -1)
      _gsourceid = g_timeout_add(500, progress_update_timer_func, this);
}

void
Decoder :: disable_progress_update ()
{
  if (_gsourceid > -1) {
    g_source_remove (_gsourceid);
    _gsourceid = -1;
  }
}
