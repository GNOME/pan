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

#include "in-memory-article.h"
#include <algorithm>
#include <cassert>
#include <config.h>
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>

using namespace pan;

InMemoryArticle ::PartState InMemoryArticle ::get_part_state() const
{
  PartState part_state(SINGLE);

  // not a multipart
  if (! is_binary)
  {
    part_state = SINGLE;
  }

  // someone's posted a followup to a multipart
  else if (! is_line_count_ge(250) && has_reply_leader(subject.to_view()))
  {
    part_state = SINGLE;
  }

  else
  {
    const InMemoryParts::number_t total = parts.get_total_part_count();
    const InMemoryParts::number_t found = parts.get_found_part_count();
    if (! found) // someone's posted a "000/124" info message
    {
      part_state = SINGLE;
    }
    else // a multipart..
    {
      part_state = total == found ? COMPLETE : INCOMPLETE;
    }
  }

  return part_state;
}

unsigned int InMemoryArticle ::get_crosspost_count() const
{
  quarks_t groups;
  foreach_const (InMemoryXref, xref, xit)
  {
    groups.insert(xit->group);
  }
  return (int)groups.size();
}

bool InMemoryArticle ::has_reply_leader(StringView const &s)
{
  return ! s.empty() && s.len > 4 && (s.str[0] == 'R' || s.str[0] == 'r')
         && (s.str[1] == 'E' || s.str[1] == 'e') && s.str[2] == ':'
         && s.str[3] == ' ';
}

unsigned long InMemoryArticle ::get_byte_count() const
{
  unsigned long bytes = 0;
  for (part_iterator it(pbegin()), end(pend()); it != end; ++it)
  {
    bytes += it.bytes();
  }
  return bytes;
}

bool InMemoryArticle ::is_byte_count_ge(unsigned long test) const
{
  unsigned long bytes = 0;
  for (part_iterator it(pbegin()), end(pend()); it != end; ++it)
  {
    if (((bytes += it.bytes())) >= test)
    {
      return true;
    }
  }
  return false;
}

InMemoryArticle ::mid_sequence_t InMemoryArticle ::get_part_mids() const
{
  mid_sequence_t mids;
  for (part_iterator it(pbegin()), end(pend()); it != end; ++it)
  {
    mids.push_back(it.mid());
  }
  return mids;
}

// const Quark&
// InMemoryArticle :: get_attachment () const
//{
//   for (part_iterator it(pbegin()), end(pend()); it!=end; ++it)
//     if (it.mid() == search)
// }

void InMemoryArticle ::clear()
{
  message_id.clear();
  author.clear();
  subject.clear();
  time_posted = 0;
  xref.clear();
  score = 0;
  parts.clear();
  is_binary = false;
}
