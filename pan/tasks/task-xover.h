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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __TASK_XOVER__H__
#define __TASK_XOVER__H__

#include <map>
#include <vector>
#include <sstream>

#include <pan/general/compression.h>
#include <pan/data/data.h>
#include <pan/tasks/task.h>
#include <pan/tasks/nntp.h>
#include <fstream>
#include <iostream>

namespace pan
{
  /**
   * Task for downloading a some or all of a newsgroups' headers
   * @ingroup tasks
   */
  class TaskXOver: public Task, private NNTP::Listener
  {
    public: // life cycle
      enum Mode { ALL, NEW, SAMPLE, DAYS };
      TaskXOver (Data& data, const Quark& group, Mode mode=ALL, unsigned long sample_size=1000);
      virtual ~TaskXOver ();

    public: // task subclass
      virtual unsigned long get_bytes_remaining () const override;

    protected: // task subclass
      virtual void use_nntp (NNTP * nntp) override;

    private: // NNTP::Listener
      virtual void on_nntp_line (NNTP*, const StringView&) override;
      virtual void on_nntp_done (NNTP*, Health, const StringView&) override;
      virtual void on_nntp_group (NNTP*, const Quark&, Article_Count, Article_Number, Article_Number) override;

      void on_nntp_line_process (NNTP*, const StringView&);

    private: // implementation - minitasks
      struct MiniTask {
        enum Type { GROUP, XOVER };
        Type _type;
        Article_Number _low, _high;
        MiniTask (Type type, Article_Number low=static_cast<Article_Number>(0ul), Article_Number high=static_cast<Article_Number>(0ul)):
          _type(type), _low(low), _high(high) {}
      };
      typedef std::deque<MiniTask> MiniTasks_t;
      typedef std::map<Quark,MiniTasks_t> server_to_minitasks_t;
      server_to_minitasks_t _server_to_minitasks;

    private: // implementation
      Data& _data;
      const Quark _group;
      std::string _short_group_name;
      Mode _mode;
      uint64_t _sample_size;
      time_t _days_cutoff;
      bool _group_xover_is_reffed;
      typedef std::map<Quark,Article_Number> server_to_high_t;
      server_to_high_t _high;
      void update_work (bool subtract_one_from_nntp_count=false);
      std::set<Quark> _servers_that_got_xover_minitasks;
      std::map<NNTP*, Article_Number> _last_xover_number;
      unsigned long _bytes_so_far;
      unsigned long _parts_so_far;
      unsigned long _articles_so_far;
      unsigned long _total_minitasks;

      std::map<int, std::stringstream*> _streams;

  };
}

#endif
