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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef _Decoder_H_
#define _Decoder_H_

#include <list>
#include <string>
#include <vector>
#include <pan/general/locking.h>
#include <pan/general/worker-pool.h>
#include <pan/tasks/task-article.h>
#ifndef PROTOTYPES
#define PROTOTYPES
#endif
#include <uulib/uudeview.h>

namespace pan
{
  /**
   * Decodes attachments in a worker thread.
   *
   * @author Calin Culianu <calin@ajvar.org>
   * @author Charles Kerr <charles@rebelbase.com>
   * @ingroup tasks
   * @see Queue
   * @see TaskArticle
   */
  class Decoder: public WorkerPool::Worker
  {
    public:

      Decoder (WorkerPool&);

      ~Decoder ();

      typedef std::vector<std::string> strings_t;

      void enqueue (TaskArticle                    * task,
                    const Quark                    & save_path,
                    const strings_t                & input_files,
                    const TaskArticle::SaveMode    & save_mode,
                    const TaskArticle::SaveOptions & options,
                    const StringView               & filename,
                    const Article                  & article);

    public:

      typedef std::list<std::string> log_t;
      log_t log_severe, log_errors, log_infos, file_errors;
      bool mark_read;
      Health health;

    protected: // inherited from WorkerPool::Worker

      void do_work() override;

    private:

      TaskArticle * task;
      std::string save_path;
      strings_t input_files;
      TaskArticle::SaveMode save_mode;
      TaskArticle::SaveOptions options;
      StringView attachment_filename;
      Quark article_subject;

      // These are set in the worker thread and polled in the main thread.
      Mutex mut;
      volatile double percent;
      std::string current_file; // the current file we are decoding, with path
      volatile int num_scanned_files;

      static void uu_log(void *thiz, char *message, int severity);
      double get_percentage (const uuprogress& p) const;
      static int uu_busy_poll(void * self, uuprogress *p);
      /** tell our task about the decode's progress */
      static gboolean progress_update_timer_func(gpointer decoder);

    protected:

      WorkerPool& _worker_pool;
      int _gsourceid;
      void disable_progress_update();
      void enable_progress_update();
  };
}

#endif
