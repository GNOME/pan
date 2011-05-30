/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2007 Calin Culianu <calin@ajvar.org>
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
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
#ifndef _Encoder_H_
#define _Encoder_H_

#include <list>
#include <string>
#include <vector>
#include <pan/general/locking.h>
#include <pan/general/worker-pool.h>
#include <pan/tasks/task-upload.h>
extern "C" {
#  define PROTOTYPES
#  include <uulib/uudeview.h>
#  include <uulib/crc32.h>
};

namespace pan
{
  /**
   * Encodes attachments (YEnc) for posting to usenet groups.
   *
   * @author Heinrich Mueller <eddie_v@gmx.de>
   * @author Calin Culianu <calin@ajvar.org>
   * @author Charles Kerr <charles@rebelbase.com>
   * @ingroup tasks
   * @see Queue
   * @see TaskArticle
   */
  class Encoder: public WorkerPool::Worker
  {
    public:

      Encoder (WorkerPool&);

      ~Encoder ();

      typedef std::vector<std::string> strings_t;

      void enqueue (TaskUpload                * task,
                    std::string               & filename,
                    std::string               & basename,
                    std::string               & groups,
                    std::string               & subject,
                    std::string               & author,
                    const TaskUpload::EncodeMode    & enc = TaskUpload::YENC);

    public:

      typedef std::list<std::string> log_t;
      log_t log_severe, log_errors, log_infos, file_errors;

    protected: // inherited from WorkerPool::Worker

      void do_work();

    private:

      std::set<int>* parts;
      friend class TaskUpload;
      friend class PostUI;
      TaskUpload * task;
      TaskUpload::EncodeMode encode_mode;
      std::string   filename;
      std::string   basename;
      std::string subject, author, groups;

      // These are set in the worker thread and polled in the main thread.
      Mutex mut;
      volatile double percent;
      std::string current_file; // the current file we are decoding, with path
      int total_parts;

      static void uu_log(void *thiz, char *message, int severity);
      double get_percentage (const uuprogress& p) const;
      static int uu_busy_poll(void * self, uuprogress *p);
      /** tell our task about the decode's progress */
      static gboolean progress_update_timer_func(gpointer decoder);

      WorkerPool& _worker_pool;
      int _gsourceid;
      void disable_progress_update();
      void enable_progress_update();
  };
}

#endif
