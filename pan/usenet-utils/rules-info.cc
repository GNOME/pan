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

#include <iostream>
#include <pan/general/debug.h>


#include <config.h>
extern "C" {
  #include <glib.h>
  #include <glib/gi18n.h>
}
#include <pan/general/macros.h>
#include "rules-info.h"

using namespace pan;

/***
****
***/

void
RulesInfo :: clear ()
{
  _type = RulesInfo::TYPE_ERR;
  _aggregates.clear ();
  _lb = _hb = 0;
  _ge = 0;
  _negate = false;
}

void
RulesInfo :: set_type_is (Type type) {
   clear ();
   _type = type;
}

void
RulesInfo :: set_type_ge (Type type, unsigned long ge) {
  clear ();
  _type = type;
  _ge = ge;
}

void
RulesInfo :: set_type_le (Type type, unsigned long le) {
  clear ();
  _type = type;
  _negate = true;
  _ge = le+1;  // le N == !ge N+1
}

void
RulesInfo :: set_type_bounds (Type type, int low, int high)
{
  clear ();
  _type = type;
  _lb = low; _hb = high;
}

void
RulesInfo :: set_type_aggregate_and () {
   clear ();
   _type = AGGREGATE_AND;
}
void
RulesInfo :: set_type_aggregate_or () {
   clear ();
   _type = AGGREGATE_OR;
}

/****
*****
****/


void
RulesInfo :: set_type_mark_read_b (int lb, int hb)
{
   set_type_bounds (MARK_READ, lb, hb);
}

void
RulesInfo :: set_type_autocache_b (int lb, int hb)
{
   set_type_bounds (AUTOCACHE, lb, hb);
}

void
RulesInfo :: set_type_dl_b  (int lb, int hb)
{
   set_type_bounds (AUTODOWNLOAD, lb, hb);
}

void
RulesInfo :: set_type_delete_b  (int lb, int hb)
{
   set_type_bounds (DELETE, lb, hb);
}
