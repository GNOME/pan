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
      /** OK -- No error yet */
      OK =  0,

      /** Task failed for a transient reason like network failure.
          The queue should leave this task as it is so that it
          can retry itself when the network clears up. */
      RETRY = -1,

      /** Task failed for a non-transient reason, such as
          the article has expired from the news server
          The queue should stop the task and let the user
          decide what to do. */
      FAIL = -2
   };
};

#endif
