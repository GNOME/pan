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

#ifndef _TaskArticle_h_
#define _TaskArticle_h_

#include <map>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>

namespace pan
{
  /**
   * Task for downloading, and optionally decoding, articles
   * @ingroup tasks
   */
  class TaskArticle: public Task, private NNTP::Listener
  {
    public: // life cycle

      enum SaveMode { NONE=0, DECODE=(1<<0), RAW=(1<<1) };

      TaskArticle (const ServerRank   & server_rank,
                   const GroupServer  & group_server,
                   const Article      & article,
                   ArticleCache       & cache,
                   ArticleRead        & read,
                   Task::Listener     * l=0,
                   SaveMode             save_mode = NONE,
                   const Quark        & save_path = Quark());
      virtual ~TaskArticle ();
      time_t get_time_posted () const { return _time_posted; }
      const Quark& get_save_path () const { return _save_path; }
      const Article& get_article () const { return _article; }
        
    public: // Task subclass
      virtual unsigned long get_bytes_remaining () const;

    private: // Task subclass
      virtual void use_nntp (NNTP * nntp);

    private: // NNTP::Listener subclass
      virtual void on_nntp_line  (NNTP*, const StringView&);
      virtual void on_nntp_done  (NNTP*, Health, const StringView&);

    protected:
      const Quark _save_path;
      const ServerRank& _server_rank;
      ArticleCache& _cache;
      ArticleRead& _read;
      quarks_t _servers;
      const Article _article;
      const time_t _time_posted;
      void on_finished ();

    private: // implementation
      bool _finished_proc_has_run;
      const SaveMode _save_mode;
      typedef std::map<Quark,int> stats_t;
      stats_t _stats;

    private:
      struct Needed {
        Article::Part part;
        NNTP * nntp;
        Xref xref;
        typedef std::vector<char> buf_t;
        buf_t buf;
        int rank;
        Needed (): nntp(0), rank(1) {}
      };
      typedef std::vector<Needed> needed_t;
      needed_t _needed;

      void update_work ();
  };
}

#endif
