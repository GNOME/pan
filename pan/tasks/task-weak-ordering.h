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

#ifndef __TASK_WEAK_ORDERING__H__
#define __TASK_WEAK_ORDERING__H__

#include <cassert>
#include <pan/general/quark.h>
#include <pan/tasks/task.h>
#include <pan/tasks/task-article.h>

namespace pan
{
  /**
   * A StrictWeakOrdering binary predicate for tasks.
   *
   * @ingroup tasks
   */
  struct TaskWeakOrdering
  {
    const Quark BODIES, CANCEL, GROUPS, POST, SAVE, XOVER, UPLOAD;
    TaskWeakOrdering ():
      BODIES ("BODIES"),
      CANCEL ("CANCEL"),
      GROUPS ("GROUPS"),
      POST ("POST"),
      SAVE ("SAVE"),
      XOVER ("XOVER"),
      UPLOAD ("UPLOAD")
      {}

    int get_rank_for_type (const Quark& type) const
    {
      int rank (0);

      if (type==BODIES || type==POST || type==CANCEL)
        rank = 0;
      else if (type==XOVER || type==GROUPS)
        rank = 1;
      else if (type==SAVE)
        rank = 2;
      else if (type==UPLOAD)
        rank = 3;

      return rank;
    }

    bool operator() (const Task* a, const Task* b) const
    {
      const Quark& a_type (a->get_type ());
      const Quark& b_type (b->get_type ());

      const int a_rank (get_rank_for_type(a_type));
      const int b_rank (get_rank_for_type(b_type));
      if (a_rank != b_rank)
        return a_rank < b_rank;

      if (a_type == SAVE) { // order 'save' by oldest
        const TaskArticle* _a = dynamic_cast<const TaskArticle*>(a);
        const TaskArticle* _b = dynamic_cast<const TaskArticle*>(b);

        if (!_a || !_b) return false;

        const time_t a_time (_a->get_time_posted ());
        const time_t b_time (_b->get_time_posted ());
        if (a_time != b_time)
          return a_time < b_time;
      }

      return false;
    }
  };
}

#endif
