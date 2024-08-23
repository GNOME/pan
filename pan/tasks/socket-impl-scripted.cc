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

#include "socket-impl-scripted.h"

#include <config.h>
#include <cassert>
#include <cstdlib>
#include <pan/general/debug.h>
#include <pan/general/messages.h>
#include <pan/general/string-view.h>

namespace pan {

const std::string ScriptedSocket::ABORT ("[[[ABORT]]]");
const std::string ScriptedSocket::NETWORK_ERROR ("[[[NETWORK ERROR]]]");

ScriptedSocket :: ScriptedSocket ()
{
}

ScriptedSocket :: ~ScriptedSocket ()
{
}

bool
ScriptedSocket :: open (const StringView& address UNUSED, int port UNUSED)
{
  // FIXME: always succeeds right now; should have an ar in ctor to make it fail
  return true;
}

void ScriptedSocket :: write_command (const StringView& chars, Listener * l)
{
   if (_script.empty()) {
      std::cerr << "UNEXPECTED [" << chars << "]\n";
      abort();
   }

   command_and_responses_t cat (_script.front());
   _script.pop_front ();

   // did they send the right command?
   if (cat.first != chars.to_string()) {
      std::cerr << "EXPECTED [" << cat.first << "];, got [" << chars << ']' << std::endl;
      abort ();
   }

   // do they want all the responses?
   strings_t& responses (cat.second);
   bool listener_wants_more = true;
   while (listener_wants_more && !responses.empty()) {
      const std::string s (responses.front ());
      responses.pop_front ();
      if (s == NETWORK_ERROR) {
         listener_wants_more = false;
         l->on_socket_error (this);
      } else if (s == ABORT) {
         listener_wants_more = false;
         l->on_socket_abort (this);
      }
      else {
         pan_debug ("got [" << cat.first << "], writing [" << s << ']');
         listener_wants_more = l->on_socket_response (this, s);
      }
   }

   if (!responses.empty()) // listener quit too soon
      abort ();
}

void
ScriptedSocket :: add_script (const std::string& command, const strings_t& responses)
{
   _script.push_back (script_t::value_type(command, responses));
}

void
ScriptedSocket :: add_script (const std::string& command, const std::string& response)
{
   strings_t responses;
   responses.push_back (response);
   add_script (command, responses);
}

bool
ScriptedSocket :: empty () const
{
   return _script.empty ();
}

void
ScriptedSocket :: clear ()
{
   _script.clear ();
}

}
