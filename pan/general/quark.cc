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

#include "quark.h"

#include <config.h>

#include <iostream>
#include <string>
#include "string-view.h"

namespace pan {

Quark::lookup_t Quark::_lookup;

/***
****
***/

void
Quark :: dump (std::ostream& o)
{
  if (size()) {
    o << "Existing Quarks: " << size() << ':' << "\n";
    for (lookup_t::const_iterator it(_lookup.begin()), e(_lookup.end()); it!=e; ++it) {
        o << "  [" << it->to_view() << "] (refcount " << it->refcount << ')' << "\n";
    }
    o << "\n";
  }
}

std::ostream&
operator<< (std::ostream& os, const Quark& s)
{
  const StringView& v (s.to_view());
  os.write (v.str, v.len);
  return os;
}

}
