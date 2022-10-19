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

#ifndef _TaskArticle_h_
#define _TaskArticle_h_

#include <pan/general/worker-pool.h>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/server-info.h>
#include <pan/data/data.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>

namespace pan
{
  struct Decoder;
  class Data;

  /**
   * Task for downloading, and optionally decoding, articles
   * @ingroup tasks
   */
  class TaskArticle: public Task,
                     private NNTP::Listener,
                     private WorkerPool::Worker::Listener
  {
    public: // life cycle

      enum SaveOptions
      {
        SAVE_ALL,
        SAVE_AS
      };

      enum ArticleActionType
      {
        ACTION_TRUE,
        ACTION_FALSE,
        NO_ACTION,
        NEVER_MARK,
        ALWAYS_MARK // TODO implement this in prefs (mark articles read after download ....)
      };

      enum SaveMode { NONE=0, DECODE=(1<<0), RAW=(1<<1) };

      TaskArticle (const ServerRank   & server_rank,
                   const GroupServer  & group_server,
                   const Article      & article,
                   ArticleCache       & cache,
                   ArticleRead        & read,
                   const ArticleActionType&  mark_read_action,
                   Progress::Listener* l=nullptr,
                   SaveMode             save_mode = NONE,
                   const Quark        & save_path = Quark(),
                   const char         * filename=nullptr,
                   const SaveOptions  & options=SAVE_ALL);
      virtual ~TaskArticle ();
      time_t get_time_posted () const { return _time_posted; }
      const Quark& get_save_path () const { return _save_path; }
      void  set_save_path (const Quark& q) { _save_path = q;}
      const Article& get_article () const { return _article; }
      const std::string& get_groups () const { return _groups; }
      const bool start_paused () const { return _paused; }
      void set_start_paused (bool val) { _paused = val; }

    public: // Task subclass
      unsigned long get_bytes_remaining () const override;
      void stop () override;

      /** only call this for tasks in the NEED_DECODE state
       * attempts to acquire the saver thread and start saving
       * returns false if failed or true if the save process started
       * (intended to be used with the Queue class). If true is returned,
       * a side-effect is that the task is now in the DECODING state.
       */
      void use_decoder (Decoder*) override;

    private: // Task subclass
      void use_nntp (NNTP * nntp) override;

    private: // NNTP::Listener subclass
      void on_nntp_line  (NNTP*, const StringView&) override;
      void on_nntp_done  (NNTP*, Health, const StringView&) override;

    private: // WorkerPool::Listener interface
      void on_worker_done (bool cancelled) override;

    protected:
      Quark _save_path;
      const ServerRank& _server_rank;
      ArticleCache& _cache;
      ArticleRead& _read;
      quarks_t _servers;
      const Article _article;
      const time_t _time_posted;
      StringView _attachment;
      ArticleActionType _mark_read_action;

    private: // implementation
      const SaveMode _save_mode;
      friend class Decoder;
      Decoder * _decoder;
      bool _decoder_has_run;
      std::string _groups;
      const SaveOptions _options;
      bool _paused;

      struct Needed {
        std::string message_id;
        unsigned long bytes;
        NNTP * nntp;
        Xref xref;
        typedef std::vector<char> buf_t;
        buf_t buf;
        int rank;
        Needed ():
          bytes(0),
          nntp(nullptr),
          rank(1)
        {}

        void reset() {
          buf_t tmp;
          buf.swap (tmp); // deallocates space
          nntp = nullptr;
        }
      };
      typedef std::vector<Needed> needed_t;
      needed_t _needed;

      void update_work (NNTP* checkin_pending=nullptr);
  };
}

#endif
