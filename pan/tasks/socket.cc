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

#include <stdarg.h>
#include <config.h>
#include <ctime>
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/string-view.h>
#include "socket.h"

using namespace pan;

Socket :: Socket ():
  _byte_count (0ul),
  _time_started (time(0)),
  _abort_flag (false)
{
}

void
Socket :: set_abort_flag (bool b)
{
   _abort_flag = b;
}

bool 
Socket :: is_abort_set () const
{
   return _abort_flag;
}

double
Socket :: get_speed_KiBps () const
{
  const time_t now (time(0));
  const int diff_secs (std::max ((time_t)1, now-_time_started));
  return (_byte_count/1024.0) / diff_secs;
}

void
Socket :: reset_speed_counter ()
{
  _byte_count = 0ul;
  _time_started = time (0);
}

void
Socket :: increment_xfer_byte_count (unsigned long byte_count)
{
   _byte_count += byte_count;
}

void
Socket :: write_command_va (Listener * l, const char *fmt, ...)
{
   va_list args;
   va_start (args, fmt);
   char * str = g_strdup_vprintf (fmt, args);
   va_end (args);
   write_command (str, l);
   g_free (str);
}
