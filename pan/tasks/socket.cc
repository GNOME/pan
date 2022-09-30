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
#include <cstdarg>
#include <ctime>
#include <cmath>
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/string-view.h>
#include "socket.h"

using namespace pan;

Socket :: Socket ():
  _bytes_since_last_check (0),
  _time_of_last_check (time(nullptr)),
  _speed_KiBps (0.0),
  _abort_flag (false),
  _id(0)
{
}

Socket :: ~Socket ()
{}

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
  const time_t now (time(nullptr));

  if (now > _time_of_last_check)
  {
    const int delta = now - _time_of_last_check;
    const double current_speed = (_bytes_since_last_check/1024.0) / delta;
    _time_of_last_check = now;
    _bytes_since_last_check = 0;

    _speed_KiBps = (std::fabs(_speed_KiBps)<0.0001)
      ? current_speed // if no previous speed, no need to smooth
      : (_speed_KiBps*0.8 + current_speed*0.2); // smooth across 5 readings
  }

  return _speed_KiBps;
}

void
Socket :: reset_speed_counter ()
{
  _time_of_last_check = time(nullptr);
  _bytes_since_last_check = 0;
}

void
Socket :: increment_xfer_byte_count (unsigned long byte_count)
{
   _bytes_since_last_check += byte_count;
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
