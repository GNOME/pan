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
  _negate = false;
  _needs_body = false;
}

void
RulesInfo :: set_type_is (Type type) {
   clear ();
   _type = type;
}

void
RulesInfo :: set_type_le (Type type, unsigned long le) {
  clear ();
  _type = type;
  _negate = true;
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
RulesInfo :: set_type_mark_read ()
{
   set_type_is (MARK_READ);
}

