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

#ifndef __ScriptedSocket_h__
#define __ScriptedSocket_h__

#include <deque>
#include <pan/tasks/socket.h>

namespace pan
{
   /**
    * A mock socket suitable for automated tests.
    *
    * This object reads from a script and, if the commands passed to it match
    * the script, it quotes back its lines from the script.  If the commands
    * don't match, it explodes.
    */
   class ScriptedSocket: public Socket
   {
      public:

         ScriptedSocket ();
         virtual ~ScriptedSocket ();
         virtual bool open (const StringView& address, int port);
         void write_command (const StringView& chars, Listener *) override;

         typedef std::deque<std::string> strings_t;
         static const std::string NETWORK_ERROR;
         static const std::string ABORT;
         void add_script (const std::string& command, const strings_t& responses);
         void add_script (const std::string& command, const std::string& response);
         bool empty () const;
         void clear ();

      private:

         typedef std::pair<std::string,strings_t> command_and_responses_t;
         typedef std::deque<command_and_responses_t> script_t;
         script_t _script;
   };
};

#endif
