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

#ifndef _Health_h_
#define _Health_h_

namespace pan
{
  /**
   * Possible health states of a Task.
   *
   * @ingroup tasks
   */
  enum Health
  {
    /** The task's health is fine. */
    OK,

    /** The task has failed because of a bad connection.
        The queue should leave this task as-is so that it
        can retry when the network clears up. */
    ERR_NETWORK,

    /** The server has rejected a command sent by this task.
        For example, an expired article can't be retrieved
        or an article can't be posted due to no permissions.
        The queue should stop the task (but let other tasks
        continue) and let the user decide how to proceed. */
    ERR_COMMAND,

    /** The task has failed because of some local
        environment problem, such as disk full.
        Further tasks are likely to fail for the
        same reason, so the queue should go offline
        until the user intervenes to fix the problem. */
    ERR_LOCAL
  };
}

#endif
