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

#include <iostream>
#include <pan/general/debug.h>


#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/macros.h>
#include "rules-info.h"

using namespace pan;

/***
****
***/

/*
 * Copy-and-swap idiom according to
 * http://stackoverflow.com/questions/3279543/what-is-the-copy-and-swap-idiom
 */

RulesInfo :: RulesInfo (const RulesInfo &that)
  : _type(that._type)
  , _ge(that._ge)
  , _lb(that._lb)
  , _hb(that._hb)
{
  foreach_const (aggregatesp_t, that._aggregates, it) {
    _aggregates.push_back (new RulesInfo(**it));
  }
}

void
swap (RulesInfo &first, RulesInfo &second)
{
  using std::swap;

  swap (first._type,       second._type);
  swap (first._aggregates, second._aggregates);
  swap (first._ge,         second._ge);
  swap (first._lb,         second._lb);
  swap (first._hb,         second._hb);
}

RulesInfo &RulesInfo::operator = (RulesInfo other)
{
  swap (*this, other);

  return *this;
}

RulesInfo :: ~RulesInfo ()
{
  foreach (aggregatesp_t, _aggregates, it) {
    delete *it;
  }
}

void
RulesInfo :: clear ()
{
  _type = RulesInfo::TYPE__ERR;
  foreach (aggregatesp_t, _aggregates, it) {
    delete *it;
  }
  _aggregates.clear ();
  _lb = _hb = 0;
  _ge = 0;
}

void
RulesInfo :: set_type_is (RulesType type) {
   clear ();
   _type = type;
}

void
RulesInfo :: set_type_ge (RulesType type, unsigned long ge) {
  clear ();
  _type = type;
  _ge = ge;
}

void
RulesInfo :: set_type_le (RulesType type, unsigned long le) {
  clear ();
  _type = type;
  _ge = le+1;  // le N == !ge N+1
}

void
RulesInfo :: set_type_bounds (RulesType type, int low, int high)
{
  clear ();
  _type = type;
  _lb = low; _hb = high;
}

void
RulesInfo :: set_type_aggregate () {
   clear ();
   _type = AGGREGATE_RULES;
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
   set_type_bounds (DELETE_ARTICLE, lb, hb);
}
