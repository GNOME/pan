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

#ifndef __MESSAGE_CHECK_H__
#define __MESSAGE_CHECK_H__

#include <string>
#include <set>
#include <gmime/gmime-message.h>
#include <pan/general/quark.h>

namespace pan
{
  /**
   * Used to check a message's correctness before being posted.
   * Mild errors result in a warning; severe errors result in Pan's refusal to post.
   * @ingroup usenet_utils
   */
  class MessageCheck
  {
    public:

      /**
       * Convenience class to specify whether an article being checked
       * is OKAY, or it deserves a warning, or Pan should refuse to post it.
       */
      struct Goodness {
        enum { OKAY=0, WARN, REFUSE };
        int state;
        Goodness(): state(OKAY) {}
        void clear()           { state = OKAY; }
        void raise_to_warn()   { if (state<WARN) state = WARN; }
        void raise_to_refuse() { if (state<REFUSE) state = REFUSE; }
        bool is_ok () const { return state == OKAY; }
        bool is_warn () const { return state == WARN; }
        bool is_refuse () const { return state == REFUSE; }
      };

      typedef std::set<std::string> unique_strings_t;

      static void message_check (const GMimeMessage * message,
                                 const StringView   & attribution,
                                 const quarks_t     & groups_our_server_has,
                                 unique_strings_t   & errors,
                                 Goodness           & goodness,
                                 bool                 binpost = false);
  };
}

#endif
