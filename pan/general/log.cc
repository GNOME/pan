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
#include <iostream>
#include <cstdarg>
#include <cstdio> // vsnprintf
#include "log.h"

using namespace pan;

/***
***/

void
Log :: clear ()
{
   _entries.clear ();
   fire_cleared ();
}

/***
***/

void
Log :: fire_entry_added (const Entry& e) {
  for (listeners_t::const_iterator i(_listeners.begin()), end(_listeners.end()); i!=end; )
    (*i++)->on_log_entry_added (e);
}

void
Log :: fire_cleared () {
  for (listeners_t::const_iterator i(_listeners.begin()), end(_listeners.end()); i!=end; )
    (*i++)->on_log_cleared ();
}

/***
***/

void
Log :: add_entry(Entry& e, std::deque<Entry>& list)
{
  _entries.resize (_entries.size() + 1);
  Entry& a (_entries.back());
  a.date = time(NULL);
  a.severity = e.severity;
  a.message = e.message;
  foreach (std::deque<Entry>, list, it)
  {
    Entry* new_entry = new Entry(*it);
    a.messages.push_back(new_entry);
  }
  fire_entry_added (a);
}

void
Log :: add (Severity severity, const char * msg)
{
  _entries.resize (_entries.size() + 1);
  Entry& e (_entries.back());
  e.date = time(NULL);
  e.severity = severity;
  e.message = msg;
  fire_entry_added (e);
}

void
Log :: add_va (Severity severity, const char * fmt, ...)
{
   if (fmt != NULL)
   {
      va_list args;
      va_start (args, fmt);
      char buf[4096];
      vsnprintf (buf, sizeof(buf), fmt, args);
      va_end (args);
      add (severity, buf);
   }
}

void
Log :: add_info_va (const char * fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  char buf[4096];
  vsnprintf (buf, sizeof(buf), fmt, args);
  va_end (args);

  add_info (buf);
}

void
Log :: add_err_va (const char * fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  char buf[4096];
  vsnprintf (buf, sizeof(buf), fmt, args);
  va_end (args);

  add_err (buf);
}

void
Log :: add_urgent_va (const char * fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  char buf[4096];
  vsnprintf (buf, sizeof(buf), fmt, args);
  va_end (args);

  add_urgent (buf);
}
