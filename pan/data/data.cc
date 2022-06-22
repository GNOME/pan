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
#include "data.h"

using namespace pan;

/****
*****
****/

void
Data :: add_listener (Listener * l)
{
   _listeners.insert (l);
}

void
Data :: remove_listener (Listener * l)
{
   _listeners.erase (l);
}

/***
****
***/

void
Data :: fire_grouplist_rebuilt ()
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_grouplist_rebuilt ();
}

void
Data :: fire_group_counts (const Quark& group, Article_Count unread, Article_Count total)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; ) {
    (*it++)->on_group_counts (group, unread, total);
  }
}

void
Data :: fire_group_read (const Quark& group)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_group_read (group);
}

void
Data :: fire_group_subscribe (const Quark& group, bool sub)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_group_subscribe (group, sub);
}

void
Data :: fire_article_flag_changed (articles_t& a, const Quark& group)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_article_flag_changed (a, group);
}

void
Data :: fire_group_entered (const Quark& group, Article_Count unread, Article_Count total)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_group_entered (group, unread, total);

}
