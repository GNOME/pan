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
#include <pan/general/locking.h>
#include <pan/data/article.h>
//#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <pan/general/log.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>
#include <set>

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
    public:

      void set_lpf (const int& setme ) { _lines_per_file = setme; }

      typedef std::vector<Quark> mid_sequence_t;

      struct Needed {
        std::string filename;
        unsigned long bytes;
        int partno;
        NNTP* nntp;
        bool partial;
        Needed (): nntp(0), bytes(0) , partial(false) {}
        void reset() { nntp = 0; }
      };

      typedef std::map<int, Needed> needed_t;

      enum EncodeMode
      {
        YENC = 0,
        BASE64,
        PLAIN
      };

      // life cycle
      TaskUpload ( const std::string         & filename,
                   const Quark               & server,
//                   ArticleCache              & cache,
                   quarks_t                  & groups,
                   std::string                 subject,
                   std::string                 author,
                   needed_t                  * imported=0,
                   Progress::Listener        * listener= 0,
                   TaskUpload::EncodeMode enc= YENC);
      virtual ~TaskUpload ();

    public: // Task subclass
      unsigned long get_bytes_remaining () const;
      void stop ();
      const std::string& basename()  { return  _basename; }
      const std::string& filename()  { return  _filename; }
      const std::string& subject ()  { return  _subject;  }
      unsigned long get_byte_count() { return _bytes;     }
      needed_t& needed()             { return _needed;    }

      /** only call this for tasks in the NEED_ENCODE state
       * attempts to acquire the encoder thread and start encoding
       * returns false if failed or true if the encoding process started
       * (intended to be used with the Queue class). If true is returned,
       * a side-effect is that the task is now in the ENCODING state.
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
      Quark _server;

    private: // implementation
      friend class Encoder;
      friend class PostUI;
      friend class NZB;
      Encoder * _encoder;
      bool _encoder_has_run;
      int _lines_per_file;
      std::string _filename;
      std::string _basename;
      TaskUpload::EncodeMode _encode_mode;
      quarks_t _groups;
      std::string _subject, _author;
      int _total_parts, _needed_parts;
      unsigned long _bytes;
      Mutex mut;
//      ArticleCache& _cache;
      std::deque<Log::Entry> _logfile;   // for intermediate updates
      Log :: Severity _severity_final;

      void build_needed_tasks(bool);

    private:
      needed_t       _needed;
      void update_work (NNTP * checkin_pending = 0);

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
