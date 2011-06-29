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
#include <ostream>
#include <fstream>
#include <sstream>

extern "C" {
#define PROTOTYPES
#include <uulib/uudeview.h>
#include <glib/gi18n.h>
#include <sys/time.h>
};

#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include "encoder.h"

using namespace pan;


Encoder :: Encoder (WorkerPool& pool):
  _worker_pool (pool),
  _gsourceid (-1)
{}


Encoder :: ~Encoder()
{
  disable_progress_update();
}

/***
****
***/

void
Encoder :: enqueue (TaskUpload                      * task,
                    EncodeCache                     * cache,
                    Article                         * article,
                    std::string                     & filename,
                    std::string                     & basename,
                    std::string                     & subject,
                    int                               lpf,
                    const TaskUpload::EncodeMode      enc)

{
  disable_progress_update ();

  this->task = task;
  this->basename = basename;
  this->filename = filename;
  this->encode_mode = enc;
  this->needed = &task->_needed;
  this->cache = cache;
  this->article = article;
  this->lpf = lpf;
  this->subject = subject;

  percent = 0;
  current_file.clear ();
  log_infos.clear();
  log_errors.clear();

  // gentlemen, start your encod'n...
  _worker_pool.push_work (this, task, false);
}

void
Encoder :: do_work()
{

  const int bufsz = 4096;
  char buf[bufsz], buf2[bufsz];
  int cnt(1);
  crc32_t crcptr;
  struct stat sb;
  std::string s;
  FILE* outfile, * infile ;

    enable_progress_update();

    int res;
    if (((res = UUInitialize())) != UURET_OK)
    {
      log_errors.push_back(_("Error initializing uulib")); // log error
    } else {
      UUSetMsgCallback (this, uu_log);
      UUSetBusyCallback (this, uu_busy_poll, 200);

      PartBatch batch;
      char cachename[4096];
      int cnt(1);
      Article* tmp = article;

      batch.init(StringView(basename), needed->size(), 0);

      /* build real subject line for article*/
      tmp->subject = subject;

      for (TaskUpload::needed_t::iterator it = needed->begin(); it != needed->end(); ++it, ++cnt)
      {
        FILE * fp = cache->get_fp_from_mid(it->second.message_id);
        if (!fp)
        {
          g_snprintf(buf, bufsz, _("Error loading %s from cache."), it->second.message_id.c_str());
          log_errors.push_back(buf); // log error
          continue;
        }

        crc32_t crc;
        int enc(YENC_ENCODED);
        std::ofstream out;
        std::string txt;
        /* (encoding!=UU_ENCODED&&encoding!=XX_ENCODED&&encoding!=B64ENCODED&&
       encoding!=PT_ENCODED&&encoding!=QP_ENCODED&&encoding!=YENC_ENCODED)) { */
        switch (encode_mode)
        {
            case TaskUpload::YENC:
                enc = YENC_ENCODED;
                break;
            case TaskUpload::PLAIN:
                file :: get_text_file_contents (filename, txt);
                cache->get_filename(cachename, Quark(it->second.message_id));
                out.open(cachename, std::ios::out);
                out << txt;
                out.close();
                res = UURET_OK;
                goto _no_encode;
                break;
            case TaskUpload::BASE64:
                enc = B64ENCODED;
                break;
            default:
                enc = YENC_ENCODED;
                break;
        }

        res = UUEncodePartial (fp, NULL, (char*)filename.c_str(), enc , (char*)basename.c_str(), NULL, 0644, cnt, lpf,&crc);

_no_encode:
        if (fp) fclose(fp);
        if (res != UURET_CONT && res != UURET_OK) break;
        cache->finalize(it->second.message_id);
        cache->get_filename(cachename, Quark(it->second.message_id));
        stat (cachename, &sb);
        it->second.bytes  = sb.st_size;
        task->_all_bytes += sb.st_size;
        //dbg
        batch.add_part(cnt, StringView(s), 0);//sb.st_size);
        if (res != UURET_CONT) break;
      }

      if (res != UURET_OK && res != UURET_CONT)
      {
        g_snprintf(buf, bufsz,
                   _("Error encoding %s: %s"),
                   basename.c_str(),
                   (res==UURET_IOERR)
                   ?  file::pan_strerror (UUGetOption (UUOPT_ERRNO, NULL,
                                                       NULL, 0))
                   : UUstrerror(res));
        log_errors.push_back(buf); // log error
      } else
      { // prepare article for upload list
        tmp->set_parts(batch);
        task->_upload_list.push_back(tmp);
      }
    UUCleanUp ();
    }
  disable_progress_update();

}

/***
****
***/

void
Encoder :: uu_log (void* data, char* message, int severity)
{
  Encoder *self = static_cast<Encoder *>(data);
  if (self->was_cancelled()) return;
  char * pch (g_locale_to_utf8 (message, -1, 0, 0, 0));

  if (severity >= UUMSG_WARNING)
    self->file_errors.push_back (pch ? pch : message);

  if (severity >= UUMSG_ERROR)
    self->log_errors.push_back (pch ? pch : message);
  else if (severity >= UUMSG_NOTE)
    self->log_infos.push_back (pch ? pch : message);

  g_free (pch);
}

double
Encoder :: get_percentage (const uuprogress& p) const
{
  // don't know if this is accurate, but i just take a guess ;)
  static const double WEIGHT_SCANNING = 50;
  static const double WEIGHT_ENCODING = 50;

  double base = 0;

  if (p.action != UUACT_SCANNING)
    base += WEIGHT_SCANNING;
  else {
    const double percent = (100.0 + p.percent);
    return base + (percent / (100.0/WEIGHT_SCANNING));
  }

  if (p.action != UUACT_ENCODING)
    base += WEIGHT_ENCODING;
  else {
    // uudeview's documentation is wrong:
    // the total percentage isn't (100*partno-percent)/numparts,
    // it's (100*(partno-1) + percent)/numparts
    const double percent = ((100.0 * (p.partno-1)) + p.percent) / p.numparts;
    return base + (percent / (100.0/WEIGHT_ENCODING));
  }

  return 0;
}

int
Encoder :: uu_busy_poll (void * d, uuprogress *p)
{
  Encoder * self (static_cast<Encoder*>(d));
  self->mut.lock();
    self->percent = self->get_percentage(*p);
    self->current_file = p->curfile;
    self->parts = p->numparts;
  self->mut.unlock();

  return self->was_cancelled(); // returning true tells uulib to abort
}

// this is called in the main thread
gboolean
Encoder :: progress_update_timer_func (gpointer decoder)
{
  Encoder *self = static_cast<Encoder *>(decoder);
  Task *task = self->task;
  if (!task || self->was_cancelled()) return false;

  self->mut.lock();
    const double percent (self->percent);
    const std::string f (content_to_utf8 (self->current_file));
  self->mut.unlock();

  task->set_step(int(percent));
  task->set_status_va (_("Encoding %s"), f.c_str());

  return true; // keep timer func running
}

/***
****
***/

void
Encoder :: enable_progress_update ()
{
  if (_gsourceid == -1)
      _gsourceid = g_timeout_add(500, progress_update_timer_func, this);
}

void
Encoder :: disable_progress_update ()
{
  if (_gsourceid > -1) {
    g_source_remove (_gsourceid);
    _gsourceid = -1;
  }
}
