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

#ifndef __TASK_XZVER_TEST_H__
#define __TASK_XZVER_TEST_H__

#include <map>
#include <vector>

#include <pan/data/data.h>
#include <pan/tasks/task.h>
#include <pan/tasks/nntp.h>

namespace pan
{
  /**
   * Task for downloading a some or all of a newsgroups' headers
   * @ingroup tasks
   */
  class TaskXZVerTest: public Task, private NNTP::Listener
  {
    public: // life cycle
      TaskXZVerTest (Data& data, const Quark& server);
      virtual ~TaskXZVerTest ();

    public: // task subclass
      virtual unsigned long get_bytes_remaining () const { return 0ul;}

    protected: // task subclass
      virtual void use_nntp (NNTP * nntp);

    private: // NNTP::Listener
      virtual void on_nntp_line (NNTP*, const StringView&);
      virtual void on_xover_follows (NNTP*, const StringView&);
      virtual void on_what (NNTP*, const StringView&);
      virtual void on_nntp_done (NNTP*, Health, const StringView&);
      virtual void on_nntp_group (NNTP*, const Quark&, unsigned long, uint64_t, uint64_t);

    private: // implementation
      Data& _data;
      const Quark _server;
  };
}

#endif
