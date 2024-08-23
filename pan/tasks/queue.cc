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
#include "queue.h"
#include "task.h"

/***
****
***/

// TODO Mulithreading downloads!

using namespace pan;

Queue :: task_states_t :: task_states_t ()
  : _decoding (NULL)
  , _encoding (NULL)
{
}

Queue :: Queue (ServerInfo         & server_info,
                TaskArchive        & archive,
                Data               & data,
                SocketCreator      * socket_creator,
                CertStore          & certstore,
                Prefs              & prefs,
                WorkerPool         & pool,
                bool                 online,
                int                  save_delay_secs):
  _server_info (server_info),
  _is_online (online),
  _socket_creator (socket_creator),
  _worker_pool (pool),
  _decoder (pool),
  _encoder (pool),
  _decoder_task (nullptr),
  _encoder_task (nullptr),
  _save_delay_secs (save_delay_secs),
  _needs_saving (false),
  _last_time_saved (0),
  _archive (archive),
  _uploads_total(0),
  _downloads_total(0),
  _certstore(certstore),
  _prefs (prefs)
{

  data.set_queue(this);

  tasks_t tasks;
  _archive.load_tasks (tasks);
  add_tasks (tasks, BOTTOM);

  _tasks.add_listener (this);
}

Queue :: ~Queue ()
{
  _tasks.remove_listener (this);

  foreach (pools_t, _pools, it)
    delete it->second;
  _pools.clear ();

  clean_n_save();

  foreach (TaskSet, _tasks, it)
    delete *it;
}

void
Queue :: on_pool_has_nntp_available (const Quark& server)
{
  Task * task = find_first_task_needing_server (server);
  if (task != nullptr)
    process_task (task);
}

void
Queue :: on_pool_error (const Quark& server UNUSED, const StringView& message)
{
  fire_queue_error (message);
}

NNTP_Pool&
Queue :: get_pool (const Quark& servername)
{
  NNTP_Pool * pool (nullptr);

  pools_t::iterator it (_pools.find (servername));
  if (it != _pools.end())
  {
    pool = it->second;
  }
  else // have to build one
  {
    pool = new NNTP_Pool (servername, _server_info, _prefs, _socket_creator, _certstore);
    pool->add_listener (this);
    _pools[servername] = pool;
  }

  return *pool;
}

/***
****
***/
void
Queue :: clean_n_save ()
{

  const tasks_t tmp (_tasks.begin(), _tasks.end());
  // remove completed tasks.
  foreach_const (tasks_t, tmp, it) {
    Task * task  (*it);
    const Task::State& state (task->get_state());
    if (state._work==Task::COMPLETED || _removing.count(task))
      remove_task (task);
  }

  // maybe save the task list.
  // only do it once in awhile to prevent thrashing.
  const time_t now (time(nullptr));
  if (_needs_saving && _last_time_saved<(now-_save_delay_secs)) {
    _archive.save_tasks (tmp);
    _needs_saving = false;
    _last_time_saved = time(nullptr);
  }
}

void
Queue :: upkeep ()
{
  clean_n_save();

  const tasks_t tmp (_tasks.begin(), _tasks.end());

  // do upkeep on the first queued task.
  // the CPU goes crazy if we run upkeep on _all_ queued tasks,
  // but we need to run upkeep on at least one queued task
  // to wake up the queue if it's stuck from a bad connection.
  // ref #352170, #354779
  foreach_const (tasks_t, tmp, it) {
    Task * task (*it);
    if (task->get_state()._work == Task::NEED_NNTP ) {
      process_task (task);
      break;
    }
  }

  // upkeep on running tasks... this lets us pop open
  // extra connections if the task can handle >1 connection
  std::set<Task*> active;
  foreach (nntp_to_task_t, _nntp_to_task, it)
    active.insert (it->second);
  foreach (std::set<Task*>, active, it)
    process_task (*it);

  // idle socket upkeep
  foreach (pools_t, _pools, it)
    it->second->idle_upkeep ();

  // maybe fire counts changed events...
  fire_if_counts_have_changed ();
}

void
Queue :: fire_if_counts_have_changed ()
{
  // if our number of connections has changed, fire an event...
  static int previous_count (-1);
  int count (0);
  foreach_const (pools_t, _pools, it) {
    int active, idle, pending, max;
    it->second->get_counts (active, idle, pending, max);
    count += (active + idle + pending);
  }
  const bool count_changed (previous_count != count);
  previous_count = count;
  if (count_changed)
    fire_connection_count_changed (count);

  // if our number of tasks has changed, fire an event...
  static int prev_active (-1);
  static int prev_total (-1);
  int active, total;
  get_task_counts (active, total);
  const bool counts_changed (active!=prev_active || total!=prev_total);
  prev_active = active;
  prev_total = total;
  if (counts_changed)
    fire_size_changed (active, total);
}

void
Queue :: get_task_counts (int& active, int& total)
{
  std::set<Task*> active_tasks;
  foreach_const (nntp_to_task_t, _nntp_to_task, it)
    active_tasks.insert (it->second);
  if (_decoder_task)
    active_tasks.insert (_decoder_task);
  if (_encoder_task)
    active_tasks.insert (_encoder_task);
  active = active_tasks.size ();
  total = _tasks.size ();
}

void
Queue :: give_task_a_decoder (Task * task)
{
  const bool was_active (task_is_active (task));
  _decoder_task = task;
  if (!was_active)
    fire_task_active_changed (task, true);
  task->give_decoder (this, &_decoder); // it's active now...
}

void
Queue :: give_task_a_encoder (Task * task)
{
  const bool was_active (task_is_active (task));
  _encoder_task = task;
  if (!was_active)
    fire_task_active_changed (task, true);
  task->give_encoder (this, &_encoder); // it's active now...
}

void
Queue :: give_task_a_connection (Task * task, NNTP * nntp)
{
  const bool was_active (task_is_active (task));
  _nntp_to_task[nntp] = task; // it's active now...
  if (!was_active)
    fire_task_active_changed (task, true);
  nntp->_socket->reset_speed_counter ();
  task->give_nntp (this, nntp);
}

void
Queue :: give_task_an_upload_slot (TaskUpload* task)
{
  int max (_server_info.get_server_limits(task->_server));
  if (_uploads.size() < max)
  {
    _uploads.insert(task);
    task->wakeup();
    fire_task_active_changed (task, true);
    process_task(task);
  }
}

void
Queue :: give_task_a_download_slot (TaskArticle* task)
{
  int max (8);//DBG!!(_server_info.get_server_limits(task->_server));
  if (_downloads.size() < max)
  {
    _downloads.insert(task);
    task->wakeup();
    fire_task_active_changed (task, true);
    process_task(task);
  }
}

void
Queue :: process_task (Task * task)
{
  pan_return_if_fail (task != nullptr);

  pan_debug ("in process_task with a task of type " << task->get_type());

  const Task::State& state (task->get_state());

  if (state._work == Task::COMPLETED)
  {
    pan_debug ("completed");
    remove_task (task);
  }
  else if (_removing.count(task))
  {
    pan_debug ("removing");
    remove_task (task);
  }
  else if (_stopped.count(task))
  {
    pan_debug ("stopped");
    task->stop();
  }
  else if (state._health == ERR_COMMAND || state._health == ERR_LOCAL)
  {
    pan_debug ("fail");
    // do nothing
  }
  else if (state._health==ERR_NOSPACE)
  {
    pan_debug ("no space");
    set_online(false);
  }
  else if (state._work == Task::WORKING)
  {
    pan_debug ("working");
  }
  else if (state._work == Task::INITIAL)
  {
    TaskUpload* t = dynamic_cast<TaskUpload*>(task);
    if (t)
      give_task_an_upload_slot(t);
    // todo multihtreading for taskarticle
//    TaskArticle* t2 = dynamic_cast<TaskArticle*>(task);
//    if (t2)
//      give_task_a_download_slot(t2);
  }
  else if (state._work == Task::NEED_DECODER)
  {
    if (!_decoder_task)
      give_task_a_decoder (task);
  }
  else if (state._work == Task::NEED_ENCODER)
  {
    if (!_encoder_task)
      give_task_a_encoder (task);
  }

  else while (_is_online && (state._work == Task::NEED_NNTP))
  {
    pan_debug("online");
    // make the requests...
    const Task::State::unique_servers_t& servers (state._servers);
    foreach_const (Task::State::unique_servers_t, servers, it)
    {
      if (_certstore.in_blacklist(*it)) continue;
      get_pool(*it).request_nntp (_worker_pool);
    }

    Quark server;
    if (!find_best_server (servers, server))
    {
      pan_debug("break");
      break;
    }

    NNTP * nntp (get_pool(server).check_out ());
    if (!nntp)
    {
      pan_debug("break");
      break;
    }

    give_task_a_connection (task, nntp);
  }
}

/***
****
***/

Task*
Queue :: find_first_task_needing_decoder ()
{
  foreach (TaskSet, _tasks, it) {
    const Task::State& state ((*it)->get_state ());
    if  ((state._work == Task::NEED_DECODER)
      && (!_stopped.count (*it))
      && (!_removing.count (*it)))
      return *it;
  }

  return nullptr;
}

Task*
Queue :: find_first_task_needing_encoder ()
{
  foreach (TaskSet, _tasks, it) {
    const Task::State& state ((*it)->get_state ());
    if  ((state._work == Task::NEED_ENCODER)
      && (!_stopped.count (*it))
      && (!_removing.count (*it)))
      return *it;
  }

  return nullptr;
}

Task*
Queue :: find_first_task_needing_server (const Quark& server)
{
  foreach (TaskSet, _tasks, it) {
    const Task::State& state ((*it)->get_state ());
    if  (state._health != ERR_COMMAND && state._health != ERR_LOCAL
      && state._health != ERR_NOSPACE
      && (state._work == Task::NEED_NNTP)
      && (state._servers.count(server))
      && (!_stopped.count (*it))
      && (!_removing.count (*it)))
      return *it;
  }

  return nullptr;
}

bool
Queue :: find_best_server (const Task::State::unique_servers_t& servers, Quark& setme)
{
  int max_score (0);
  Quark best_server;

  foreach_const (Task::State::unique_servers_t, servers, it)
  {
    const Quark& server (*it);
    const NNTP_Pool& pool (get_pool(server));

    int score (0);
    if (_is_online) {
      int active, idle, pending, max;
      pool.get_counts (active, idle, pending, max);
      const int empty_slots (max - (idle+active));
      score = idle*10 + empty_slots;
    }

    if (score > max_score) {
      max_score = score;
      best_server = server;
    }
  }

  if (max_score)
    setme = best_server;

  return max_score!=0;
}

/***
****
***/

void
Queue :: on_set_items_added (TaskSet& container UNUSED, TaskSet::items_t& tasks, int pos)
{
  _needs_saving = true;
  fire_tasks_added (pos, tasks.size());
}

void
Queue :: on_set_item_removed (TaskSet& container UNUSED, Task*& task, int pos)
{
  _needs_saving = true;
  fire_task_removed (task, pos);
}

void
Queue :: on_set_item_moved (TaskSet& container UNUSED, Task*& task, int new_pos, int old_pos)
{
  _needs_saving = true;
  fire_task_moved (task, new_pos, old_pos);
}

/***
****
***/

void
Queue :: fire_queue_error (const StringView& message)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_error (*this, message);
}
void
Queue :: fire_tasks_added (int pos, int count)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_tasks_added (*this, pos, count);
}
void
Queue :: fire_task_removed (Task*& task, int pos)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_task_removed (*this, *task, pos);
}
void
Queue :: fire_task_moved (Task*& task, int new_pos, int old_pos)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_task_moved (*this, *task, new_pos, old_pos);
}
void
Queue :: fire_connection_count_changed (int count)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_connection_count_changed (*this, count);
}
void
Queue :: fire_size_changed (int active, int total)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_size_changed (*this, active, total);
}
void
Queue :: fire_online_changed (bool online)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_online_changed (*this, online);
}
void
Queue :: fire_task_active_changed (Task * task, bool active)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_queue_task_active_changed (*this, *task, active);
}

/***
****
***/

void
Queue :: set_online (bool online)
{
  _is_online = online;
  fire_online_changed (_is_online);

  if (_is_online)
    upkeep ();
}

void
Queue :: stop_tasks (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it) {
    Task * task (*it);
    if (_tasks.index_of (task) != -1) {
      _stopped.insert (task);
      process_task (task);

      TaskArticle* ta = dynamic_cast<TaskArticle*>(*it);
      if (ta)
      {
        ta->set_start_paused(true);
      }
    }
  }
}

void
Queue :: restart_tasks (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it) {
    Task * task (*it);
    if (_tasks.index_of(task) != -1) {
      _stopped.erase (task);
      process_task (task);

      TaskArticle* ta = dynamic_cast<TaskArticle*>(*it);
      if (ta)
      {
        ta->set_start_paused(false);
      }
    }
  }
}

void
Queue :: add_task (Task * task, AddMode mode)
{
  tasks_t tasks;
  tasks.push_back (task);
  add_tasks (tasks, mode);
}

void
Queue :: add_tasks (const tasks_t& tasks, AddMode mode)
{

  foreach_const (tasks_t, tasks, it) {
    TaskArticle * ta (dynamic_cast<TaskArticle*>(*it));
    if (ta)
    {
      _mids.insert (ta->get_article().message_id);
      if (ta->start_paused())
      {
        _stopped.insert(*it);
      }
    }
  }

  if (mode == TOP)
    _tasks.add_top (tasks);
  else if (mode == BOTTOM)
    _tasks.add_bottom (tasks);
  else
    _tasks.add (tasks);

  // process all new tasks
  tasks_t tmp (tasks);
  foreach (tasks_t, tmp, it)
    process_task (*it);

  // maybe fire counts changed events...
  fire_if_counts_have_changed ();
}

bool
Queue :: contains (const Quark& mid) const
{
  return _mids.count (mid);
}

void
Queue :: remove_latest_task ()
{
  if (!_tasks.empty())
    remove_task (_tasks[_tasks.size()-1]);
}

bool
Queue :: task_is_active (const Task * task) const
{
  if (task && task==_decoder_task)
    return true;

  if (task && task==_encoder_task)
    return true;

  bool task_has_nntp (false);
  foreach_const (nntp_to_task_t, _nntp_to_task, it)
    if ((task_has_nntp = task==it->second))
      break;
  return task_has_nntp;
}

void
Queue :: remove_tasks (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it)
    remove_task (*it);
}

void
Queue :: remove_task (Task * task)
{
  const int index (_tasks.index_of (task));
  pan_return_if_fail (index != -1);

  if (task_is_active (task)) // wait for the Task to finish
  {
    pan_debug ("can't delete this task right now because it's active");
    task->stop();
    _removing.insert (task);
  }
  else // no NNTPs working, we can remove right now.
  {
    TaskArticle * ta (dynamic_cast<TaskArticle*>(task));
    if (ta)
      _mids.erase (ta->get_article().message_id);
    _stopped.erase (task);
    _removing.erase (task);
    _tasks.remove (index);

    // manually upkeep on ONE new taskupload to keep the queue going
    TaskUpload * t  (dynamic_cast<TaskUpload*>(task));
    if (t)
    {
      int max (_server_info.get_server_limits(t->_server));
      _uploads.erase(t);
      const tasks_t tmp (_tasks.begin(), _tasks.end());
      foreach_const (tasks_t, tmp, it) {
        Task * task (*it);
        if (task->get_state()._work == Task::INITIAL)
        {
          give_task_an_upload_slot(dynamic_cast<TaskUpload*>(*it));
          break;
        }
      }
    }

    delete task;
  }

  // maybe fire counts changed events...
  fire_if_counts_have_changed ();
}

void
Queue :: get_all_task_states (task_states_t& setme)
{
  setme.tasks.clear();
  setme.tasks.reserve(_tasks.size());

  std::vector<Task *> & need_decode = setme._need_decode.get_container();
  need_decode.clear();
  need_decode.reserve(setme.tasks.capacity());

  foreach(TaskSet, _tasks, it) {
    setme.tasks.push_back(*it);
    if ((*it)->get_state()._work == Task::NEED_DECODER)
      need_decode.push_back(*it);
  }
  setme._need_decode.sort();

  setme._queued.get_container() = setme.tasks;
  setme._queued.sort ();

  std::vector<Task*>& stopped (setme._stopped.get_container());
  stopped.clear ();
  stopped.insert (stopped.end(), _stopped.begin(), _stopped.end());

  std::vector<Task*>& removing (setme._removing.get_container());
  removing.clear ();
  removing.insert (removing.end(), _removing.begin(), _removing.end());

  std::vector<Task*>& running (setme._running.get_container());
  std::set<Task*> tmp;
  foreach (nntp_to_task_t, _nntp_to_task, it) tmp.insert (it->second);
  running.clear ();
  running.insert (running.end(), tmp.begin(), tmp.end());

  setme._decoding = _decoder_task;
}

void
Queue :: check_in (NNTP * nntp, Health nntp_health)
{
  Task * task (_nntp_to_task[nntp]);

  // if the same task still needs a connection to this server,
  // shoot it straight back.  This can be a lot faster than
  // returning the NNTP to the pool and checking it out again.
  const Task::State state (task->get_state ());

  if ((nntp_health != ERR_NETWORK)
    && _is_online
    && state._health != ERR_COMMAND && state._health != ERR_LOCAL
    && state._health != ERR_NOSPACE
    && (state._work == Task::NEED_NNTP)
    && !_removing.count(task)
    && state._servers.count(nntp->_server)
    && find_first_task_needing_server(nntp->_server)==task)
  {
    task->give_nntp (this, nntp);
  }
  else
  {
    // take care of our nntp counting
    Task * task (_nntp_to_task[nntp]);
    _nntp_to_task.erase (nntp);

    // notify the listeners if the task isn't active anymore...
    if (!task_is_active (task))
      fire_task_active_changed (task, false);

    // if we encountered a local error, fire an error message.
    if (state._health == ERR_LOCAL)
      fire_queue_error ("");

    // tell if we reached the end of disk space
    if (state._health == ERR_NOSPACE)
      fire_queue_error (_("No space left on device."));

    // return the nntp to the pool
    const Quark& servername (nntp->_server);
    NNTP_Pool& pool (get_pool (servername));
    pool.check_in (nntp, nntp_health);

    // what to do now with this task...
    process_task (task);
  }
}

void
Queue :: check_in (Decoder* decoder UNUSED, Task* task)
{
  // take care of our decoder counting...
  _decoder_task = nullptr;

  // notify the listeners if the task isn't active anymore...
  if (!task_is_active (task))
    fire_task_active_changed (task, false);

  // if the task hit an error, fire an error message
  const Task::State state (task->get_state ());
  if (state._health == ERR_LOCAL)
    fire_queue_error ("");

  if (state._health == ERR_NOSPACE)
    fire_queue_error (_("No space left on device."));

  // pass our worker thread on to another task
  Task * next = find_first_task_needing_decoder ();
  if (next && (next!=task))
    process_task (next);

  // what to do now with this task...
  process_task (task);
}

void
Queue :: check_in (Encoder* encoder UNUSED, Task* task)
{
  // take care of our decoder counting...
  _encoder_task = nullptr;

  // notify the listeners if the task isn't active anymore...
  if (!task_is_active (task))
    fire_task_active_changed (task, false);

  // if the task hit an error, fire an error message
  const Task::State state (task->get_state ());
  if (state._health == ERR_LOCAL)
    fire_queue_error ("");

  if (state._health == ERR_NOSPACE)
    fire_queue_error (_("No space left on device."));

  // pass our worker thread on to another task
  Task * next = find_first_task_needing_encoder ();
  if (next && (next!=task))
    process_task (next);

  // what to do now with this task...
  process_task (task);
}

void
Queue :: move_up (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_up (old_pos);
  }
}

void
Queue :: move_down (const tasks_t& tasks)
{
  foreach_const_r (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_down (old_pos);
  }
}

void
Queue :: move_top (const tasks_t& tasks)
{
  foreach_const_r (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_top (old_pos);
  }
}

void
Queue :: move_bottom (const tasks_t& tasks)
{
  foreach_const (tasks_t, tasks, it) {
    Task * task (*it);
    const int old_pos (_tasks.index_of (task));
    _tasks.move_bottom (old_pos);
  }
}

void
Queue :: get_connection_counts (int& setme_active,
                                int& setme_idle,
                                int& setme_connecting) const
{
  setme_active = setme_idle = setme_connecting = 0;

  foreach_const (pools_t, _pools, it)
  {
    int active, idle, connecting, unused;
    it->second->get_counts (active, idle, connecting, unused);
    setme_active += active;
    setme_idle += idle;
    setme_connecting += connecting;
  }
}

void
Queue :: get_full_connection_counts (std::vector<ServerConnectionCounts>& setme) const
{
  setme.resize (_pools.size());
  ServerConnectionCounts * cit = &setme.front();

  int unused;
  foreach_const (pools_t, _pools, it) {
    ServerConnectionCounts& counts = *cit++;
    counts.server_id = it->first;
    it->second->get_counts (counts.active, counts.idle, counts.connecting, unused);
    foreach_const (nntp_to_task_t, _nntp_to_task, nit)
      if (nit->first->_server == counts.server_id)
        counts.KiBps += nit->first->_socket->get_speed_KiBps ();
  }
}

double
Queue :: get_speed_KiBps () const
{
  double KiBps (0.0);
  foreach_const (nntp_to_task_t, _nntp_to_task, it)
    KiBps += it->first->_socket->get_speed_KiBps ();
  return KiBps;
}

void
Queue :: get_task_speed_KiBps (const Task  * task,
                               double      & setme_KiBps,
                               int         & setme_connections) const
{
  double KiBps (0.0);
  int connections (0);
  foreach_const (nntp_to_task_t, _nntp_to_task, it) {
    if (it->second==task) {
      ++connections;
      KiBps += it->first->_socket->get_speed_KiBps ();
    }
  }

  setme_KiBps = KiBps;
  setme_connections = connections;
}

void
Queue :: get_stats (unsigned long   & queued_count,
                    unsigned long   & running_count,
                    unsigned long   & stopped_count,
                    uint64_t        & KiB_remain,
                    double          & KiBps,
                    int             & hours_remain,
                    int             & minutes_remain,
                    int             & seconds_remain)
{
  KiB_remain = 0;
  KiBps = 0;
  queued_count = running_count = stopped_count = 0;
  hours_remain = minutes_remain = seconds_remain = 0;

  task_states_t tasks;
  get_all_task_states (tasks);
  foreach_const (tasks_t, tasks.tasks, it)
  {
    Task * task (*it);

    const Queue::TaskState state (tasks.get_state (task));
    if (state == Queue::RUNNING || state == Queue::DECODING || state == Queue::ENCODING)
      ++running_count;
    else if (state == Queue::STOPPED)
      ++stopped_count;
    else if (state == Queue::QUEUED || state == Queue::QUEUED_FOR_DECODE || state == Queue::QUEUED_FOR_ENCODE)
      ++queued_count;

    if (state==Queue::RUNNING || state==Queue::QUEUED)
      KiB_remain += task->get_bytes_remaining ();
  }

  const unsigned long task_count (running_count + queued_count);
  KiBps = get_speed_KiBps ();
  if (task_count) {
    const double KiB ((double)KiB_remain / 1024);
    unsigned long tmp (KiBps>0.01 ? ((unsigned long)(KiB / KiBps)) : 0);
    seconds_remain = tmp % 60ul; tmp /= 60ul;
    minutes_remain = tmp % 60ul; tmp /= 60ul;
    hours_remain   = tmp;
  }
}

void
Queue :: on_dl_limit_reached ()
{
  if (_prefs.get_flag("disconnect-on-dl-limit-reached", false))
    set_online (false);
}

void
Queue :: on_reset_xfer_bytes ()
{
  set_online (true);
}
