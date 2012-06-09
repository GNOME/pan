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

#ifndef __TASK_XOVERINFO__H__
#define __TASK_XOVERINFO__H__

#include <map>
#include <vector>
#include <sstream>

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
  class TaskXOverInfo: public Task, private NNTP::Listener
  {
    public: // life cycle

      typedef std::pair<uint64_t,uint64_t> xover_t;

      TaskXOverInfo (Data& data, const Quark& group);
      virtual ~TaskXOverInfo ();

    public: // task subclass
      virtual unsigned long get_bytes_remaining () const { return 0ul; }

    protected: // task subclass
      virtual void use_nntp (NNTP * nntp);

    private: // NNTP::Listener
      virtual void on_nntp_line (NNTP*, const StringView&);
      virtual void on_nntp_done (NNTP*, Health, const StringView&);
      virtual void on_nntp_group (NNTP*, const Quark&, unsigned long, uint64_t, uint64_t);

    private: // implementation - minitasks
      struct MiniTask {
        enum Type { GROUP, XOVER };
        Type _type;
        uint64_t _low, _high;
        MiniTask (Type type, uint64_t low=0ul, uint64_t high=0ul):
          _type(type), _low(low), _high(high) {}
      };
      typedef std::deque<MiniTask> MiniTasks_t;
      typedef std::map<Quark,MiniTasks_t> server_to_minitasks_t;
      server_to_minitasks_t _server_to_minitasks;

    private: // implementation
      Data& _data;
      const Quark _group;
      std::string _short_group_name;
      typedef std::map<Quark,uint64_t> server_to_high_t;
      server_to_high_t _high;
      bool _group_xover_is_reffed;
      void update_work (bool subtract_one_from_nntp_count=false);
      std::set<Quark> _servers_that_got_xover_minitasks;
      std::map<NNTP*,uint64_t> _last_xover_number;
      unsigned long _bytes_so_far;
      unsigned long _parts_so_far;
      unsigned long _articles_so_far;


  };
}

#endif
