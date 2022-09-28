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
#ifndef _Worker_Pool_H_
#define _Worker_Pool_H_

#include <set>
#include <glib.h>

namespace pan
{
  /**
   * A C++ wrapper for GThreadPool.
   *
   * @author Calin Culianu <calin@ajvar.org>
   * @see Queue
   * @ingroup general
   */
  class WorkerPool
  {
    public:

      /**
       * Creates a pool of worker threads.
       * @param num_threads -1 means no limit
       * @param exclusive if true, don't share threads with other pools
       */
      WorkerPool (int num_threads=-1, bool exclusive=false);

      /**
       * Calls each of this pool's workers' cancel_silently().
       * Use this if its worker's listeners are being deleted.
       */
      void cancel_all_silently ();

      ~WorkerPool ();

    public:

      class Worker
      {
        public:

          Worker(): pool(nullptr), listener(nullptr),
                    cancelled(false), silent(false),
                    delete_worker(false) {}

          virtual ~Worker() {}

          /** Sets the flag used in was_cancelled() */
          virtual void cancel() { cancelled = true; }

          /** Like cancel(), but also tells this worker to
              not call its Listener's on_worker_done().
              Use this if the listener is deleted. */
          void cancel_silently() { cancel(); silent=true; }

          /** Subclasses' do_work() methods should call this
              periodically and stop working if it's true. */
          virtual bool was_cancelled() const { return cancelled; }

          struct Listener {
            virtual ~Listener() {}
            virtual void on_worker_done (bool was_cancelled)=0;
          };

        protected:

          virtual void do_work () = 0;

        private:

          friend class WorkerPool;
          WorkerPool * pool;
          Listener * listener;
          volatile bool cancelled, silent;
          bool delete_worker;

          static void worker_thread_func (gpointer worker, gpointer unused);
          static gboolean main_thread_cleanup_cb (gpointer worker);
          void main_thread_cleanup ();
      };

      /**
       * Enqueues a worker so its do_work() is called <b>in a worker thread</b>.
       * After that, the following will be done <b>in the main thread</b>:
       * 1. if a listener was provided, its on_work_done() method is called.
       * 2. if delete_worker was true, the worker will be deleted.
       */
      void push_work (Worker*, Worker::Listener*, bool del_worker_when_done);

    private:

      GThreadPool * tpool;
      typedef std::set<Worker*> WorkerSet;
      WorkerSet my_workers;
      WorkerPool& operator= (WorkerPool&); // not implemented
      WorkerPool (const WorkerPool&); // not implemented
  };
}

#endif
