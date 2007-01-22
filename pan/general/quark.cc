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

#include <iostream>
#include <string>
#include "string-view.h"
#include "quark.h"

using namespace pan;

#if defined(HAVE_EXT_HASH_MAP)
// preallocate buckets.
// this might not be portable?
Quark::key_to_impl_t Quark::_lookup (300000);
#else
Quark::key_to_impl_t Quark::_lookup;
#endif

/***
****
***/

void
Quark :: dump (std::ostream& o)
{
  if (size()) {
    o << "Existing Quarks: " << size() << ':' << std::endl;
    for (key_to_impl_t::const_iterator it(_lookup.begin()), e(_lookup.end()); it!=e; ++it)
        o << "  [" << it->first << "] (refcount " << it->second.refcount << ')' << std::endl;
    o << std::endl;
  }
}

std::ostream&
pan::operator<< (std::ostream& os, const pan::Quark& s)
{
  const StringView& v (s.to_view());
  os.write (v.str, v.len);
  return os;
}
