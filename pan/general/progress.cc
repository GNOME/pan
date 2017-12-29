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
#include <cstdio> // vsnprintf
#include "progress.h"
#include "string-view.h"

using namespace pan;

/***
****
***/

void
Progress :: fire_percentage (int p) {
  for (listeners_cit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_progress_step (*this, p);
}
void
Progress :: fire_pulse () {
  for (listeners_cit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_progress_pulse (*this);
}
void
Progress :: fire_error (const StringView& msg) {
  for (listeners_cit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_progress_error (*this, msg);
}
void
Progress :: fire_finished (int status) {
  for (listeners_cit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_progress_finished (*this, status);
}
void
Progress :: fire_status (const StringView& msg) {
  for (listeners_cit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_progress_status (*this, msg);
}
void
Progress :: add_listener (Listener * l) {
   if (!l) return;
   _listeners.insert (l);
}
void
Progress :: remove_listener (Listener * l) {
   if (!l) return;
   _listeners.erase (l);
}

/***
****
***/

void
Progress :: pulse ()
{
  fire_pulse ();
}

void
Progress :: set_status (const StringView& status)
{
  _status_text = status.to_string();
  fire_status (status);
}
void
Progress :: set_status_va (const char * fmt, ...)
{
  va_list args;
  va_start (args, fmt);
  char buf[4096];
  vsnprintf (buf, sizeof(buf), fmt, args);
  va_end (args);
  set_status (buf);
}

void
Progress :: set_error (const StringView& error)
{
  _errors.push_back (error.to_string());
  fire_error (error);
}

void
Progress :: init_steps (int steps)
{
  _step = 0;
  _steps = steps;
  fire_percentage (0);
}

void
Progress :: add_steps (int steps)
{
  const int old_of_100 (get_progress_of_100());
  _steps += steps;
  const int new_of_100 (get_progress_of_100());
  if (old_of_100 != new_of_100)
    fire_percentage (new_of_100);
}

void
Progress :: set_step (int step)
{
  const int old_of_100 (get_progress_of_100());
   _step = step;
  const int new_of_100 (get_progress_of_100());

  if (old_of_100 != new_of_100)
    fire_percentage (new_of_100);
}

void
Progress :: increment_step (int increment)
{
   set_step (_step+increment);
}

int
Progress :: get_progress_of_100 () const
{
  int p = (int)(!_steps ? 0 : (_step*100ul)/_steps);
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

void
Progress :: set_finished (int status)
{
  _done = status;
  fire_finished (status);
}

std::string
Progress :: describe () const
{
   return _description;
}

Progress :: Progress (const StringView& description):
   _description (description.to_string()),
   _status_text (),
   _steps (0),
   _step (0),
   _done (0),
   _active (false)
{
}

Progress :: ~Progress ()
{
}
