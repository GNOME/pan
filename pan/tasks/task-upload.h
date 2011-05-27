/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
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

#ifndef _TaskUpload_h_
#define _TaskUpload_h_

#include <pan/general/worker-pool.h>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <pan/data/file-queue.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>
#include <deque>

namespace pan
{
  struct Encoder;

  /**
   * Task for uploading binary data to usenet
   * @ingroup tasks
   */
  class TaskUpload: public Task,
                     private NNTP::Listener,
                     private WorkerPool::Worker::Listener
  {
    public: // life cycle

      enum EncodeMode
      {
        YENC = 0,
        BASE64,
        PLAIN
      };

      TaskUpload ( const FileQueue::FileData & file_data,
                   const Quark               & server,
                   std::string                 groups,
                   std::string                 subject,
                   std::string                 author,
                   Progress::Listener        * listener=0,
                   TaskUpload::EncodeMode enc     = YENC);
      virtual ~TaskUpload ();

    public: // Task subclass
      unsigned long get_bytes_remaining () const;
      void stop ();

      /** only call this for tasks in the NEED_DECODE state
       * attempts to acquire the saver thread and start saving
       * returns false if failed or true if the save process started
       * (intended to be used with the Queue class). If true is returned,
       * a side-effect is that the task is now in the DECODING state.
       */
      virtual void use_encoder (Encoder*);

    private: // Task subclass
      virtual void use_nntp (NNTP * nntp);

    private: // NNTP::Listener subclass
      virtual void on_nntp_line  (NNTP*, const StringView&);
      virtual void on_nntp_done  (NNTP*, Health, const StringView&);

    private: // WorkerPool::Listener interface
      void on_worker_done (bool cancelled);

    protected:
      const Quark _server;

    private: // implementation
      friend class Encoder;
      Encoder * _encoder;
      bool _encoder_has_run;
      const FileQueue::FileData _file_data;
      const std::string _basename;
      TaskUpload::EncodeMode _encode_mode;
      std::string _groups, _subject, _author;
      int _parts; // filled in by encoder
      Mutex mut;

    private:
      struct Needed {
        std::string filename;
        unsigned long bytes;
        int partno;
        Needed (): partno(0) {}
      };
      typedef std::deque<Needed> needed_t;
      needed_t _needed;

      void update_work (void);
  };

// from mime-utils.cc
namespace
{

     const char*
   __yenc_extract_tag_val_char (const char * line, const char *tag)
   {
      const char * retval = NULL;

      const char * tmp = strstr (line, tag);
      if (tmp != NULL) {
         tmp += strlen (tag);
         if (*tmp != '\0')
            retval = tmp;
      }

      return retval;
   }

  guint
   __yenc_extract_tag_val_int_base (const char * line,
                                    const char * tag,
                                    int          base)
   {
      guint retval = 0;

      const char * tmp = __yenc_extract_tag_val_char (line, tag);
      if (tmp != NULL) {
         char * tail = NULL;
         retval = strtoul (tmp, &tail, base);
         if (tmp == tail)
            retval = 0;
      }

      return retval;
   }
}

}

#endif
