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
        private AdaptableSet<Task*, TaskWeakOrdering>::Listener
  {
    public:

      typedef AdaptableSet<Task*, TaskWeakOrdering> TaskSet;

      UploadQueue ();
      virtual ~UploadQueue ();

      typedef std::vector<Task*> tasks_t;
      void remove_tasks  (const tasks_t&);
      void move_up       (const tasks_t&);
      void move_down     (const tasks_t&);
      void move_top      (const tasks_t&);
      void move_bottom   (const tasks_t&);

      void select_encode (const tasks_t&);

      enum AddMode { TOP, BOTTOM };
      void add_tasks     (const tasks_t&, AddMode=BOTTOM);

      void add_task (Task*, AddMode=BOTTOM);
      void remove_task (Task*);

      void clear();

      void get_all_tasks  (std::vector<Task*>& setme);

    protected:
      virtual void fire_tasks_added  (int index, int count);
      virtual void fire_task_removed (Task*&, int index);
      virtual void fire_task_moved   (Task*&, int index, int old_index);

    public:

      struct Listener {
        virtual ~Listener () {}
        virtual void on_queue_task_active_changed (UploadQueue&, Task&, bool active) {}
        virtual void on_queue_tasks_added (UploadQueue&, int index, int count) = 0;
        virtual void on_queue_task_removed (UploadQueue&, Task&, int index) = 0;
        virtual void on_queue_task_moved (UploadQueue&, Task&, int new_index, int old_index) = 0;
      };

      void add_listener (Listener *l) { _listeners.insert(l); }
      void remove_listener (Listener *l) { _listeners.erase(l); }

    private:
      typedef std::set<Listener*> listeners_t;
      typedef listeners_t::iterator lit;
      listeners_t _listeners;

    public:
      Task* operator[](size_t i) { if (i>=_tasks.size() || i<0) return NULL; return _tasks[i]; }
      const Task* operator[](size_t i) const { if (i>=_tasks.size() || i<0) return NULL; return _tasks[i]; }

    private:
      TaskSet _tasks;
      virtual void on_set_items_added  (TaskSet&, TaskSet::items_t&, int index);
      void on_set_item_removed (TaskSet&, Task*&, int index) override;
      void on_set_item_moved   (TaskSet&, Task*&, int index, int old_index) override;
  };
}

#endif
