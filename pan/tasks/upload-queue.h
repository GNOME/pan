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

#ifndef _UploadQueue_h_
#define _UploadQueue_h_

#include <map>
#include <set>
#include <vector>
#include <pan/general/macros.h> // for UNUSED
#include <pan/general/map-vector.h>
#include <pan/tasks/decoder.h>
#include <pan/tasks/encoder.h>
#include <pan/general/quark.h>
#include <pan/tasks/nntp-pool.h>
#include <pan/tasks/socket.h>
#include <pan/tasks/adaptable-set.h>
#include <pan/tasks/task-upload.h>
#include <pan/tasks/encoder.h>
#include <pan/tasks/task-weak-ordering.h>

namespace pan
{
  class NNTP;
  class ServerInfo;
  class WorkerPool;
  struct Encoder;

  class UploadQueue:
        private AdaptableSet<TaskUpload*, TaskWeakOrdering>::Listener
  {
    public:

      typedef AdaptableSet<TaskUpload*, TaskWeakOrdering> TaskSet;

      UploadQueue ();
      virtual ~UploadQueue ();

      typedef std::vector<TaskUpload*> tasks_t;
      void remove_tasks  (const tasks_t&);
      void move_up       (const tasks_t&);
      void move_down     (const tasks_t&);
      void move_top      (const tasks_t&);
      void move_bottom   (const tasks_t&);

      enum AddMode { TOP, BOTTOM };
      void add_tasks     (const tasks_t&, AddMode=BOTTOM);

      void add_task (TaskUpload*, AddMode=BOTTOM);
      void remove_task (TaskUpload*);

      void clear();

      void get_all_tasks (tasks_t& setme);

    protected:
      virtual void fire_tasks_added  (int index, int count);
      virtual void fire_task_removed (TaskUpload*&, int index);
      virtual void fire_task_moved   (TaskUpload*&, int index, int old_index);

    public:

      struct Listener {
        virtual ~Listener () {}
        virtual void on_queue_task_active_changed (UploadQueue&, TaskUpload&, bool active) {}
        virtual void on_queue_tasks_added (UploadQueue&, int index, int count) = 0;
        virtual void on_queue_task_removed (UploadQueue&, TaskUpload&, int index) = 0;
        virtual void on_queue_task_moved (UploadQueue&, TaskUpload&, int new_index, int old_index) = 0;
        virtual void on_queue_connection_count_changed (UploadQueue&, int count) {}
        virtual void on_queue_size_changed (UploadQueue&, int active, int total) {}
        virtual void on_queue_online_changed (UploadQueue&, bool online) {}
        virtual void on_queue_error (UploadQueue&, const StringView& message) {}
      };

      void add_listener (Listener *l) { _listeners.insert(l); }
      void remove_listener (Listener *l) { _listeners.erase(l); }

    private:
      typedef std::set<Listener*> listeners_t;
      typedef listeners_t::iterator lit;
      listeners_t _listeners;

    public:
      TaskUpload* operator[](size_t i) { return _tasks[i]; }
      const TaskUpload* operator[](size_t i) const { return _tasks[i]; }

    private:
      TaskSet _tasks;
      virtual void on_set_items_added  (TaskSet&, TaskSet::items_t&, int index);
      virtual void on_set_item_removed (TaskSet&, TaskUpload*&, int index);
      virtual void on_set_item_moved   (TaskSet&, TaskUpload*&, int index, int old_index);
  };
}

#endif
