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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef _TaskUpload_h_
#define _TaskUpload_h_

#include <pan/general/worker-pool.h>
#include <pan/general/locking.h>
#include <pan/data/article.h>
#include <pan/data/encode-cache.h>
#include <pan/data/data.h>
#include <pan/general/log.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>

#ifndef PROTOTYPES
#define PROTOTYPES
#endif
#include <uulib/uudeview.h>
#include <gmime/gmime.h>
#include <glib/gi18n.h>

#include <set>

namespace pan
{
  struct Encoder;
  class Data;

  /**
   * Task for uploading binary data to usenet
   * @ingroup tasks
   */
  class TaskUpload: public Task,
                    private NNTP::Listener,
                    private WorkerPool::Worker::Listener
  {
    public:

      struct UploadInfo
      {
        std::string  save_file;
        std::string  mid;
        int bpf;
        int total;
      };

      const Article& get_article ()  { return _article; }
      const std::string& get_groups () const { return _groups; }
      const std::string& get_filename () const { return _filename; }

      typedef std::vector<Quark> mid_sequence_t;

      struct Needed {

        NNTP* nntp;
        unsigned long bytes;
        int partno;

        std::string message_id, last_mid;
        std::string mid; // for rng
        std::string cachename;
        Xref xref;
        Needed (): nntp(nullptr), bytes(0) , partno(1) {}
        void reset() { nntp = nullptr; }
      };

      typedef std::map<int, Needed> needed_t;

      // life cycle
      TaskUpload ( const std::string         & filename,
                   const Quark               & server,
                   EncodeCache               & cache,
                   Article                     article,
                   UploadInfo                  format,
                   GMimeMessage *              msg=nullptr,
                   Progress::Listener        * listener= nullptr);

      virtual ~TaskUpload ();

    public: // Task subclass
      unsigned long get_bytes_remaining () const override;
      void stop () override;
      const std::string& get_basename()  { return  _basename; }

      /** only call this for tasks in the NEED_ENCODE state
       * attempts to acquire the encoder thread and start encoding
       * returns false if failed or true if the encoding process started
       * (intended to be used with the Queue class). If true is returned,
       * a side-effect is that the task is now in the ENCODING state.
       */
      void use_encoder (Encoder*) override;

    private: // Task subclass
      void use_nntp (NNTP * nntp) override;

    private: // NNTP::Listener subclass
      void on_nntp_line  (NNTP*, const StringView&) override;
      void on_nntp_done  (NNTP*, Health, const StringView&) override;

    private: // WorkerPool::Listener interface
      void on_worker_done (bool cancelled) override;

    protected:
      Quark _server;

    private: // implementation
      friend class Encoder;
      friend class PostUI;
      friend class Queue;
      friend class NZB;
      friend class TaskPane;

      Encoder * _encoder;
      bool _encoder_has_run;
      std::string _filename;
      std::string _basename;
      std::string _subject, _master_subject, _author;
      std::string _save_file;
      int _total_parts, _needed_parts;
      unsigned long _bytes;
      EncodeCache& _cache;
      std::deque<Log::Entry> _logfile;   // for intermediate updates
      Article _article;
      unsigned long _all_bytes;
      std::vector<Article*> _upload_list;
      Article::mid_sequence_t _mids;
      int _queue_pos;
      int _bpf;
      needed_t _needed;
      std::string _references; // original references, not to be touched!
      std::string _first_mid;
      std::set<int> _wanted;
      GMimeMessage * _msg;
      void prepend_headers(GMimeMessage* msg, TaskUpload::Needed * n, std::string& d);
      void add_reference_to_list(std::string s);
      bool _first;
      std::string _groups;

      void update_work (NNTP * checkin_pending = nullptr);

    public:
      void set_encoder_done (bool setme) { _encoder_has_run = setme; }
      needed_t& needed() { return _needed; }
      void build_needed_tasks();
      void wakeup() override { _state.set_working(); update_work(); }
  };
}

#endif
