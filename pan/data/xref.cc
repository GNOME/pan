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
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/string-view.h>
#include "xref.h"

using namespace pan;

void
Xref :: insert (const Quark             & server,
                const StringView        & line)
{
  pan_return_if_fail (!server.empty());

  // trim & cleanup; remove leading "Xref: " if present
  StringView xref (line);
  xref.trim ();
  if (xref.len>6 && !memcmp(xref.str,"Xref: ",6)) {
    xref = xref.substr (xref.str+6, NULL);
    xref.trim ();
  }

  // walk through the xrefs, of format "group1:number group2:number"
  targets.reserve (targets.size() + std::count(xref.begin(), xref.end(), ':'));
  StringView s;
  while (xref.pop_token (s)) {
    if (s.strchr (':') != nullptr) {
      StringView group_name;
      if (s.pop_token(group_name, ':')) {
        Target t;
        t.server = server;
        t.group = group_name;
        t.number = Article_Number(s);
        targets.get_container().push_back (t);
      }
    }
  }

  targets.sort ();
}

void
Xref :: remove_server (const Quark& server)
{
  std::vector<Target> t;
  t.reserve (targets.size());
  foreach_const (targets_t, targets, it)
    if (it->server != server)
      t.push_back (*it);
  targets.get_container().swap (t);
}

void
Xref :: remove_targets_less_than (const Quark    & server,
                                  const Quark    & group,
                                  Article_Number   n)
{
  std::vector<Target> t;
  t.reserve (targets.size());
  foreach_const (targets_t, targets, it)
    if (it->server!=server || it->group!=group || it->number>=n)
      t.push_back (*it);
  targets.get_container().swap (t);
}

namespace
{
  // targets are equal if their servers are equal.
  // this works because servers are the primary key in Target::operator< (const Target)
  struct TargetServerStrictWeakOrdering {
    bool operator()(const Xref::Target& a, const Xref::Target& b) const {
      return a.server < b.server;
    }
  };
}

bool
Xref :: has_server (const Quark  & server) const
{
  Target tmp;
  tmp.server = server;
  return std::binary_search (targets.begin(), targets.end(), tmp, TargetServerStrictWeakOrdering());
}

bool
Xref :: find (const Quark    & server,
              Quark          & setme_group,
              Article_Number & setme_number) const
{
  Target tmp;
  tmp.server = server;
  const_iterator it (std::lower_bound (targets.begin(), targets.end(), tmp, TargetServerStrictWeakOrdering()));
  const bool found (it != targets.end());
  if (found) {
    setme_group = it->group;
    setme_number = it->number;
  }
  return found;
}

Article_Number
Xref :: find_number (const Quark    & server,
                     const Quark    & group) const
{
  Target tmp;
  tmp.server = server;
  tmp.group = group;
  const_iterator it (targets.lower_bound (tmp));
  return it!=targets.end() && it->server==server && it->group==group ? it->number : static_cast<Article_Number>(0ull);
}

void
Xref :: get_servers (quarks_t & addme) const
{
  foreach_const (targets_t, targets, it)
    addme.insert (it->server);
}
