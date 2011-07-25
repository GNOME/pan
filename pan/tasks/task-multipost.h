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

#ifndef _TaskMultiPost_h_
#define _TaskMultiPost_h_

#include <pan/general/locking.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/general/log.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>
#include <pan/tasks/task-upload.h>

extern "C" {
  #define PROTOTYPES
  #include <uulib/uudeview.h>
  #include <glib/gi18n.h>
};

#include <set>

namespace pan
{
  /**
   * Task for uploading binary data to usenet
   * @ingroup tasks
   */
  class TaskMultiPost: public Task,
                    private NNTP::Listener

  {
    public:

      const Article& get_article ()  { return _article; }

      typedef std::vector<Quark> mid_sequence_t;

      struct Needed {
        unsigned long bytes;
        int partno;
        NNTP* nntp;
        std::string message_id;
        std::string cachename;
        Xref xref;
        Needed (): nntp(0), bytes(0) , partno(1) {}
        void reset() { nntp = 0; }
      };

      typedef std::map<int, TaskMultiPost::Needed> needed_t;

      // life cycle
      TaskMultiPost ( quarks_t                  & filenames,
                      const Quark               & server,
                      Article                     article,
                      GMimeMultipart *              msg,
                      Progress::Listener        * listener= 0);

      virtual ~TaskMultiPost ();

    public: // Task subclass
      unsigned long get_bytes_remaining () const;
      void stop ();

      /** only call this for tasks in the NEED_ENCODE state
       * attempts to acquire the encoder thread and start encoding
       * returns false if failed or true if the encoding process started
       * (intended to be used with the Queue class). If true is returned,
       * a side-effect is that the task is now in the ENCODING state.
       */
    private: // Task subclass
      virtual void use_nntp (NNTP * nntp);

    private: // NNTP::Listener subclass
      virtual void on_nntp_line  (NNTP*, const StringView&);
      virtual void on_nntp_done  (NNTP*, Health, const StringView&);


    protected:
      Quark _server;

    private: // implementation
      friend class PostUI;
      friend class UploadQueue;

      quarks_t _filenames;
      std::string _basename;
      std::string _subject, _master_subject, _author;
      std::string _save_file;
      unsigned long _bytes;
      Article _article;
      unsigned long _all_bytes;
      std::string mid;


      void dbg() ;

      Article::mid_sequence_t _mids;
      TaskMultiPost::needed_t _needed;

      void update_work (NNTP * checkin_pending = 0);

    public:
      needed_t& needed() { return _needed; }
      void build_needed_tasks();

    private:
      std::set<int> _wanted;
      GMimeMultipart * _msg;
      long _files_left;

  };
}

#endif
