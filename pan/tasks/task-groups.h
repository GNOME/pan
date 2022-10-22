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

#ifndef __TASK_GROUPS_H__
#define __TASK_GROUPS_H__

#include <pan/general/quark.h>
#include <pan/data/data.h>
#include <pan/tasks/task.h>
#include <pan/data/cert-store.h>
#include <pan/tasks/nntp.h>

namespace pan
{
  /**
   * Task for downloading the grouplist of a new server.
   * @ingroup tasks
   */
  class TaskGroups:
    public Task,
    private NNTP::Listener
  {
    public: // life cycle
      TaskGroups (Data& data, const Quark& server);
      virtual ~TaskGroups ();

    public: // Task's virtual functions
      unsigned long get_bytes_remaining () const override { return 0; }

    protected: // Task's virtual functions
      virtual void use_nntp (NNTP * nntp) override;

    private: // NNTP::Listener's virtual functions
      void on_nntp_line (NNTP*, const StringView&) override;
      void on_nntp_line_process (NNTP*, const StringView&);
      void on_nntp_done (NNTP*, Health, const StringView&) override;

    private: // implementation
      Data& _data;
      Quark _servername;
      typedef std::map<Quark,Data::NewGroup> new_groups_t;
      new_groups_t _new_groups;
      unsigned long _group_count;

      enum Step { LIST, LIST_NEWSGROUPS, DONE };
      Step _step;

      std::stringstream stream;
  };
};

#endif
