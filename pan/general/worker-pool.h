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
#ifndef _Worker_Pool_H_
#define _Worker_Pool_H_

#include <glib/gtypes.h>
#include <glib/gthreadpool.h>
#include <set>

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

      /** Creates a pool of num_threads, -1 means no limit, if exclusive then 
          the threads are always running and not shared with other pools 
          (as in glib documentation) */
      WorkerPool(int num_threads = -1, bool exclusive = false); 

      ~WorkerPool(); ///< deletes all the threads, may or may not stop workers
    
      /** Call this on app exit to gracelessly notify all workers to die 
       * and not clean up after themselves, etc */
      static void quitAllWorkers(); 

      class Worker
      {
        public:
          Worker();
          virtual ~Worker();

          /** Notify of stop request -- called by stopAllWorkers(), 
              Re-implement if you think you can force a better stop in your 
              do_work function, otherwise it sets the flag please_stop,
              which a particular implementation of this class may or may
              not listen to in order to abort work early. */
          virtual void cancel() { please_stop = true; }
          /** Makes the worker quit ASAP and not notify any listeners.
              Call this if you think the listeners will be destroyed or are 
              already destroyed or when the app is exiting. */
          virtual void gracelessly_quit() { cancel(); quit = true; }

          virtual bool was_cancelled() const { return please_stop; }
          virtual bool was_gracelessly_quit() const { return quit; }

        public:

          struct Listener {
            virtual ~Listener() {};
            /** Called in the context of the main thread after enqueued work is done.
                `data' points to the data passed in when the work was enqueued. */
            virtual void on_work_complete(void * data) = 0;
            virtual void on_work_cancelled(void *data) {}
          };

        protected:

          /** Re-implement in your classes to actually do the work.  Called,
              by WorkerPool framework in a worker thread. */
          virtual void do_work(void * data) = 0;

          /** workers implementing do_work() should check this flag and if flag 
              set, worker should try and stop what it was doing */
          volatile bool please_stop, quit;
          friend class WorkerPool;
      };

      /** Calls w->do_work(data) for you in the worker thread.
          When the work is completed, the optional listener is notified
          *from the main thread*.  Optionally you can tell this function
          to delete the Worker when the work is done as well.  If deleting,
          the delete is done after the listener is notified in the main thread.*/
      void push_work(Worker *w, void * data = 0, Worker::Listener * = 0, bool delete_worker_on_completion = false);

    private:
      struct Work; 

      static void thr_wrapper(gpointer data, gpointer user_data);
      static gboolean finalize(gpointer data); /// called in main thread
      void doWork(Work *); /**< Called in the context of the 
                            worker thread to do work, deletes work when done */

      mutable GThreadPool *tpool;

      typedef std::set<Worker *> WorkerSet;
      typedef std::set<Work *>   WorkSet;
      WorkerSet my_workers;
      WorkSet   my_work;

      /// guarded access to all_workers set -- may initialize heap-allocated static data
      static WorkerSet & all_workers();  

      WorkerPool& operator= (WorkerPool&); // not implemented
  };
}

#endif
