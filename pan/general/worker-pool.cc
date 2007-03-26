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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <cassert>
#include <glib/gmain.h> // for g_idle_add
#include <pan/general/debug.h>
#include <pan/general/foreach.h>
#include "worker-pool.h"

namespace pan
{
  
  /* static */ 
  WorkerPool::WorkerSet & 
  WorkerPool::all_workers() 
  {
    static WorkerPool::WorkerSet *all_workers_ptr = new WorkerSet;

    return *all_workers_ptr;
  }

  WorkerPool::Worker::Worker() : please_stop(false), quit(false)
  {
    all_workers().insert(this);
  }

  WorkerPool::Worker::~Worker() 
  {
    all_workers().erase(this);
  }

  void
  WorkerPool::quitAllWorkers()
  {
    debug("notifying all workers to gracelessly quit");
    foreach(WorkerSet, all_workers(), it)
      (*it)->gracelessly_quit();
  }

  struct WorkerPool::Work 
  {
    WorkerPool *pool;
    Worker *worker;
    void *data;
    Worker::Listener *listener;
    bool delete_worker;

    Work (WorkerPool *p, Worker *w, void *d, Worker::Listener *l, bool dw):
      pool(p), worker(w), data(d), listener(l), delete_worker(dw) {}
  };

  WorkerPool::WorkerPool (int nthr, bool exclusive)
  {
    assert ((!exclusive || nthr>0) && "can't have unlimited exclusive threads!");
    if (!g_thread_supported ()) g_thread_init (0);
    tpool = g_thread_pool_new (thr_wrapper,  this, nthr, exclusive, 0);
  }
  
  WorkerPool::~WorkerPool()
  {    
    debug("Deleting thread pool 0x" << ((void *)this)
           << " with max threads: " << g_thread_pool_get_max_threads(tpool)
           << " num threads: " << g_thread_pool_get_num_threads(tpool)
           << " unprocessed: " << g_thread_pool_unprocessed(tpool));

    foreach(WorkerSet, my_workers, it)
      (*it)->cancel();

    foreach(WorkSet, my_work, it)
      (*it)->pool = 0; // this is so that when work is done, it doesn't reference us, because we will have already been deleted by then!

    g_thread_pool_free (tpool, false, true); // NB: will block waiting for ALL enqueued work to complete -- this has been verified to be true even if the work doesn't yet have a thread, etc  -Calin
  }

  /* static */ 
  void WorkerPool::thr_wrapper(gpointer data, gpointer user_data)
  {
    WorkerPool *self = reinterpret_cast<WorkerPool *>(user_data);
    Work *work = reinterpret_cast<Work *>(data);
    self->doWork(work); // call class member
  }

  /* static */ 
  gboolean WorkerPool::finalize(gpointer data)
  {
    Work *work = reinterpret_cast<Work *>(data);

    if (work->worker->was_gracelessly_quit()) {

      /* check for was_gracelessly_quit is here because
         race condition is possible if checking in non-main thread */
      debug("worker was gracelessly quit, aborting without notifying listeners!");

    } else {

      if (work->listener) {
        if (work->worker->was_cancelled())
          work->listener->on_work_cancelled(work->data);
        else
          work->listener->on_work_complete(work->data);
      }

    }

    if (work->pool) { // work->pool is set to NULL if the WorkerPool was destroyed from underneath our feet! Aieeee! ;)  In that case there's no point in erasing ourselves from its list of workers anyway..
      work->pool->my_workers.erase(work->worker);
      work->pool->my_work.erase(work);
    }

    if (work->delete_worker) delete work->worker;
    delete work;
    return false; // tell main loop not to call us again
  }

  void WorkerPool::doWork(Work *work) 
  {
    /* do work here .. */
    work->worker->do_work(work->data);
    g_idle_add(finalize, work); /* deletes work, also may call listener funcs, etc */
  }

  void WorkerPool::push_work (Worker *worker, void * data, Worker::Listener *listener, bool delworker) 
  {
    Work * work = new Work (this, worker, data, listener, delworker);
    my_workers.insert (worker);
    my_work.insert(work);
    g_thread_pool_push (tpool,  work, 0); // ends up invoking thr_wrapper in thread    
  }
}
