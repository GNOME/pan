/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2007 Calin Culianu <calin@ajvar.org>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include <cassert>
#include <glib.h> // for g_idle_add
#include <gdk/gdk.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include "worker-pool.h"

using namespace pan;

WorkerPool::WorkerPool(int n, bool exclusive)
{
  assert((!exclusive || n>0) && "can't have unlimited exclusive threads!");
  tpool = g_thread_pool_new(Worker::worker_thread_func, this, n, exclusive, nullptr);
}

void
WorkerPool::cancel_all_silently()
{
  foreach (WorkerSet, my_workers, it)(*it)->cancel_silently ();
}

WorkerPool::~WorkerPool()
{
  foreach (WorkerSet, my_workers, it){
    (*it)->pool = nullptr;
    (*it)->cancel ();
  }

 // blocks until *all* workers, both running and queued, have run.
  g_thread_pool_free (tpool, false, true);
}

/***
 ****
 ***/

void
WorkerPool::push_work(Worker *w, Worker::Listener *l, bool delete_worker)
{
  w->cancelled = false;
  w->silent = false;
  w->pool = this;
  w->listener = l;
  w->delete_worker = delete_worker;
  my_workers.insert(w);
  g_thread_pool_push(tpool, w, nullptr); // jump to worker_thread_func
}

void
WorkerPool::Worker::worker_thread_func(gpointer g, gpointer unused UNUSED)
{
  static_cast<Worker*>(g)->do_work();
  g_idle_add(main_thread_cleanup_cb, g); // jump to main_thread_cleanup_cb
}

gboolean
WorkerPool::Worker::main_thread_cleanup_cb(gpointer g)
{
  static_cast<Worker*>(g)->main_thread_cleanup();
  return false; // tell main loop that we're done
}

void
WorkerPool::Worker::main_thread_cleanup()
{
  if (listener && !silent)
    listener->on_worker_done(was_cancelled());

  if (pool)
    pool->my_workers.erase(this);

  if (delete_worker)
    delete this;
}
