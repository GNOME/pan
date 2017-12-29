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
#include <cassert>
#include <algorithm>
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include "article.h"

using namespace pan;

Article :: PartState
Article :: get_part_state () const
{
  PartState part_state (SINGLE);

  // not a multipart
  if (!is_binary)
    part_state = SINGLE;

  // someone's posted a followup to a multipart
  else if (!is_line_count_ge(250) && has_reply_leader(subject.to_view()))
    part_state = SINGLE;

  else  {
    const Parts::number_t total = parts.get_total_part_count ();
    const Parts::number_t found = parts.get_found_part_count ();
    if (!found) // someone's posted a "000/124" info message
      part_state = SINGLE;
    else // a multipart..
      part_state = total==found ? COMPLETE : INCOMPLETE;
  }

  return part_state;
}

unsigned int
Article :: get_crosspost_count () const
{
  quarks_t groups;
  foreach_const (Xref, xref,  xit)
    groups.insert (xit->group);
  return (int) groups.size ();
}

bool
Article :: has_reply_leader (const StringView& s)
{
  return !s.empty()
    && s.len>4 \
    && (s.str[0]=='R' || s.str[0]=='r')
    && (s.str[1]=='E' || s.str[1]=='e')
    && s.str[2]==':'
    && s.str[3]==' ';
}

unsigned long
Article :: get_byte_count () const
{
  unsigned long bytes = 0;
  for (part_iterator it(pbegin()), end(pend()); it!=end; ++it)
    bytes += it.bytes();
  return bytes;
}

bool
Article :: is_byte_count_ge (unsigned long test) const
{
  unsigned long bytes = 0;
  for (part_iterator it(pbegin()), end(pend()); it!=end; ++it)
    if (((bytes += it.bytes())) >= test)
      return true;
  return false;
}

Article :: mid_sequence_t
Article :: get_part_mids () const
{
  mid_sequence_t mids;
  for (part_iterator it(pbegin()), end(pend()); it!=end; ++it)
  {
    mids.push_back (it.mid());
  }
  return mids;
}

//const Quark&
//Article :: get_attachment () const
//{
//  for (part_iterator it(pbegin()), end(pend()); it!=end; ++it)
//    if (it.mid() == search)
//}

void
Article :: clear ()
{
  message_id.clear ();
  author.clear ();
  subject.clear ();
  time_posted = 0;
  xref.clear ();
  score = 0;
  parts.clear ();
  is_binary = false;
}
