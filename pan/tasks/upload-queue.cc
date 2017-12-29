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

#include <config.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/data/server-info.h>
#include "upload-queue.h"
#include "task.h"
#include "task-upload.h"

/***
****
***/

using namespace pan;

UploadQueue :: UploadQueue ()
{
    _tasks.add_listener (this);
}

UploadQueue :: ~UploadQueue ()
{}

void
UploadQueue :: get_all_tasks (std::vector<Task*>& setme)
{
  setme.clear();
  setme.reserve(_tasks.size());

  foreach(TaskSet, _tasks, it)
    setme.push_back(*it);
}

void
UploadQueue :: clear()
{
  const tasks_t tmp (_tasks.begin(), _tasks.end());
  foreach_const (tasks_t, tmp, it) {
    Task * task  (*it);
    remove_task (task);
  }

  foreach (TaskSet, _tasks, it)
    delete *it;
}

void
UploadQueue :: add_task (Task * task, AddMode mode)
{
  tasks_t tasks;
  tasks.push_back (task);
  add_tasks (tasks, mode);
}

void
UploadQueue :: add_tasks (const tasks_t& tasks, AddMode mode)
{
  if (mode == TOP)
    _tasks.add_top (tasks);
  else if (mode == BOTTOM)
    _tasks.add_bottom (tasks);
  else
    _tasks.add (tasks);
}

void
UploadQueue :: remove_tasks (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it)
    remove_task (*it);
}

void
UploadQueue :: remove_task (Task * task)
{
  const int index (_tasks.index_of (task));
  pan_return_if_fail (index != -1);
  _tasks.remove (index);
  delete task;
}

void
UploadQueue :: move_up (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_up (old_pos);
  }
}

void
UploadQueue :: move_down (const tasks_t& tasks)
{
  foreach_const_r (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_down (old_pos);
  }
}

void
UploadQueue :: move_top (const tasks_t& tasks)
{
  foreach_const_r (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_top (old_pos);
  }
}

void
UploadQueue :: move_bottom (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_bottom (old_pos);
  }
}

void
UploadQueue :: select_encode (const tasks_t& tasks)
{}

void
UploadQueue :: fire_tasks_added (int pos, int count)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_tasks_added (*this, pos, count);
}
void
UploadQueue :: fire_task_removed (Task*& task, int pos)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_task_removed (*this, *task, pos);
}
void
UploadQueue :: fire_task_moved (Task*& task, int new_pos, int old_pos)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_task_moved (*this, *task, new_pos, old_pos);
}

void
UploadQueue :: on_set_items_added (TaskSet& container UNUSED, TaskSet::items_t& tasks, int pos)
{
  fire_tasks_added (pos, tasks.size());
}

void
UploadQueue :: on_set_item_removed (TaskSet& container UNUSED, Task*& task, int pos)
{
  fire_task_removed (task, pos);
}

void
UploadQueue :: on_set_item_moved (TaskSet& container UNUSED, Task*& task, int new_pos, int old_pos)
{
  fire_task_moved (task, new_pos, old_pos);
}
