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
#include <pan/data/encode-cache.h>
#include <pan/data/data.h>
#include <pan/general/log.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>

extern "C" {
#define PROTOTYPES
#include <uulib/uudeview.h>
};

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

      struct UploadInfo
      {
        bool comment1;
        std::string  save_file;
        std::string  mid;
        int bpf;
        int total;
      };

      const Article& get_article ()  { return _article; }

      typedef std::vector<Quark> mid_sequence_t;

      struct Needed {
        unsigned long bytes;
        int partno;
        NNTP* nntp;
        std::string message_id;
        std::string mid, last_mid; // for rng
        std::string cachename;
        Xref xref;
        bool encoded;
        Needed (): nntp(0), bytes(0) , partno(1), encoded(false) {}
        void reset() { nntp = 0; }
      };

      typedef std::map<int, Needed> needed_t;

      enum EncodeMode
      {
        BASE64=0,
        PLAIN=1,
        YENC=2,
      };

      // life cycle
      TaskUpload ( const std::string         & filename,
                   const Quark               & server,
                   EncodeCache               & cache,
                   Article                     article,
                   UploadInfo                  format,
                   GMimeMessage *              msg,
                   Progress::Listener        * listener= 0,
                   EncodeMode enc= YENC);

      virtual ~TaskUpload ();

    public: // Task subclass
      unsigned long get_bytes_remaining () const;
      void stop ();
      const std::string& get_basename()  { return  _basename; }
      std::string enc_mode_to_str(EncodeMode& e)
      {
        std::string res;
        switch (e)
        {
            case YENC:
              res += "yEnc";
              break;
            case BASE64:
              res += "BASE64";
              break;
            case PLAIN:
              break;
            default:
              res += "yEnc";
              break;
        }
        return res;
      }

      std::string encode_mode()  { return enc_mode_to_str(_encode_mode); }

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
      friend class Queue;
      friend class NZB;

      Encoder * _encoder;
      bool _encoder_has_run;
      std::string _filename;
      std::string _basename;
      TaskUpload::EncodeMode _encode_mode;
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

      void update_work (NNTP * checkin_pending = 0);

    public:
      void set_encoder_done (bool setme) { _encoder_has_run = setme; }
      needed_t& needed() { return _needed; }
      void build_needed_tasks();
      void wakeup() { _state.set_working(); update_work(); }

    private:
      std::set<int> _wanted;
      GMimeMessage * _msg;
      void prepend_headers(GMimeMessage* msg, TaskUpload::Needed * n, std::string& d);
      void add_reference_to_list(std::string s);

  };
}

#endif
