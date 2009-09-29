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

#ifndef _Queue_h_
#define _Queue_h_

#include <map>
#include <set>
#include <vector>
#include <pan/general/macros.h> // for UNUSED
#include <pan/general/map-vector.h>
#include <pan/general/quark.h>
#include <pan/tasks/decoder.h>
#include <pan/tasks/nntp-pool.h>
#include <pan/tasks/socket.h>
#include <pan/tasks/adaptable-set.h>
#include <pan/tasks/task.h>
#include <pan/tasks/task-weak-ordering.h>

namespace pan
{
  class NNTP;
  class ServerInfo;
  class WorkerPool;

  /**
   * A Queue helper that saves tasks to disk and restores them from disk.
   *
   * @ingroup tasks
   * @see NZB
   */
  struct TaskArchive {
    virtual ~TaskArchive () { }
    virtual void save_tasks (const std::vector<Task*>& saveme UNUSED) { }
    virtual void load_tasks (std::vector<Task*>& setme UNUSED) { }
  };

  /**
   * Queues up tasks to be processed by NNTP objects as news server
   * connections become available.
   *
   * @ingroup tasks
   */
  class Queue:
    public NNTP::Source,
    public Task::DecoderSource,
    private NNTP_Pool::Listener,
    private AdaptableSet<Task*, TaskWeakOrdering>::Listener
  {
    public:
      Queue (ServerInfo&, TaskArchive&, Socket::Creator*, WorkerPool&,
             bool online, int save_delay_secs);
      virtual ~Queue ();

      typedef std::vector<Task*> tasks_t;
      void stop_tasks    (const tasks_t&);
      void restart_tasks (const tasks_t&);
      void remove_tasks  (const tasks_t&);
      void move_up       (const tasks_t&);
      void move_down     (const tasks_t&);
      void move_top      (const tasks_t&);
      void move_bottom   (const tasks_t&);

      enum AddMode { AGE, TOP, BOTTOM };
      void add_tasks     (const tasks_t&, AddMode=AGE);

      void set_online (bool online);
      void add_task (Task*, AddMode=AGE);
      void remove_task (Task*);
      void remove_latest_task ();
      bool is_online () const { return _is_online; }
      void upkeep ();
      bool contains (const Quark& message_id) const;

      double get_speed_KiBps () const;
      void get_task_speed_KiBps (const Task*, double& setme_KiBps, int& setme_connections) const;
      void get_connection_counts (int& setme_active, int& setme_idle, int& setme_connecting) const;
      struct ServerConnectionCounts {
        Quark server_id;
        int active, idle, connecting;
        double KiBps;
        ServerConnectionCounts(): active(0), idle(0), connecting(0), KiBps(0.0) {}
      };
      void get_full_connection_counts (std::vector<ServerConnectionCounts>& setme) const;
                                         

    public:
      enum TaskState { QUEUED, RUNNING, DECODING, STOPPED, REMOVING,
                       QUEUED_FOR_DECODE };

      /**
       * An ordered collection of tasks and their corresponding TaskState s.
       */
      struct task_states_t {
        friend class Queue;
        private:
          typedef sorted_vector<Task*,true> sorted_tasks_t;
          sorted_tasks_t _queued;
          sorted_tasks_t _stopped;
          sorted_tasks_t _running;
          sorted_tasks_t _removing;
          sorted_tasks_t _need_decode;
          Task * _decoding;
        public:
          tasks_t tasks;
          TaskState get_state (Task* task) const {
            if (_decoding && (task==_decoding)) return DECODING;
            if (_removing.count(task)) return REMOVING;
            if (_stopped.count(task)) return STOPPED;
            if (_running.count(task)) return RUNNING;
            if (_need_decode.count(task)) return QUEUED_FOR_DECODE;
            if (_queued.count(task)) return QUEUED;
            return STOPPED;
          }
      };
      Task* operator[](size_t i) { return _tasks[i]; }
      const Task* operator[](size_t i) const { return _tasks[i]; }
      void get_all_task_states (task_states_t& setme);
      void get_task_counts (int& active, int& total);
      void get_stats (unsigned long   & queued_count,
                      unsigned long   & running_count,
                      unsigned long   & stopped_count,
                      uint64_t        & KiB_remain,
                      double          & KiBps,
                      int             & hours_remain,
                      int             & minutes_remain,
                      int             & seconds_remain);

    public:

      /** Interface class for objects that listen to a Queue's events */
      struct Listener {
        virtual ~Listener () {}
        virtual void on_queue_task_active_changed (Queue&, Task&, bool active) = 0;
        virtual void on_queue_tasks_added (Queue&, int index, int count) = 0;
        virtual void on_queue_task_removed (Queue&, Task&, int index) = 0;
        virtual void on_queue_task_moved (Queue&, Task&, int new_index, int old_index) = 0;
        virtual void on_queue_connection_count_changed (Queue&, int count) = 0;
        virtual void on_queue_size_changed (Queue&, int active, int total) = 0;
        virtual void on_queue_online_changed (Queue&, bool online) = 0;
        virtual void on_queue_error (Queue&, const StringView& message) = 0;
      };
      void add_listener (Listener *l) { _listeners.insert(l); }
      void remove_listener (Listener *l) { _listeners.erase(l); }

    public: // inherited from NNTP::Source
      virtual void check_in (NNTP*, Health);

    public: // inherited from Task::DecoderSource
      virtual void check_in (Decoder*, Task*);

    private: // inherited from NNTP_Pool::Listener
      virtual void on_pool_has_nntp_available (const Quark& server);
      virtual void on_pool_error (const Quark& server, const StringView& message);

    protected:
      void process_task (Task *);
      void give_task_a_decoder (Task*);
      void give_task_a_connection (Task*, NNTP*);
      ServerInfo& _server_info;
      bool _is_online;
      Task* find_first_task_needing_server (const Quark& server);
      Task* find_first_task_needing_decoder ();
      bool find_best_server (const Task::State::unique_servers_t& servers, Quark& setme);
      bool task_is_active (const Task*) const;

      typedef std::map<NNTP*,Task*> nntp_to_task_t;
      nntp_to_task_t _nntp_to_task;

      std::set<Task*> _removing;
      std::set<Task*> _stopped;
      Socket::Creator * _socket_creator;
      WorkerPool & _worker_pool;
      Decoder _decoder;
      Task * _decoder_task;

    protected:
      virtual void fire_tasks_added  (int index, int count);
      virtual void fire_task_removed (Task*&, int index);
      virtual void fire_task_moved   (Task*&, int index, int old_index);

    private:
      typedef std::set<Listener*> listeners_t;
      typedef listeners_t::iterator lit;
      listeners_t _listeners;
      void fire_if_counts_have_changed ();
      void fire_task_active_changed (Task*, bool);
      void fire_connection_count_changed (int count);
      void fire_size_changed (int active, int total);
      void fire_online_changed (bool online);
      void fire_queue_error (const StringView& message);

    private:
      typedef Loki::AssocVector<Quark,NNTP_Pool*> pools_t;
      pools_t _pools;
      NNTP_Pool& get_pool (const Quark& server);
      void request_connections (const quarks_t& servers);

    private:
      /** don't save tasks.nzb more frequently than this setting */
      int _save_delay_secs;
      bool _needs_saving;
      time_t _last_time_saved;
      quarks_t _mids;

    private:
      TaskArchive& _archive;

    private:
      typedef AdaptableSet<Task*, TaskWeakOrdering> TaskSet;
      TaskSet _tasks;
      virtual void on_set_items_added  (TaskSet&, TaskSet::items_t&, int index);
      virtual void on_set_item_removed (TaskSet&, Task*&, int index);
      virtual void on_set_item_moved   (TaskSet&, Task*&, int index, int old_index);
  };
}

#endif
