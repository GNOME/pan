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
#include <cassert>
#include <algorithm>
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/foreach.h>
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

  // someone's posted a "000/124" info message
  else if (parts.empty())
    part_state = SINGLE;

  // a multipart
  else {
    part_state = COMPLETE;
    for (Article::parts_t::const_iterator it(parts.begin()), end(parts.end()); part_state==COMPLETE && it!=end; ++it)
      if (it->empty())
        part_state = INCOMPLETE;
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
Article :: is_byte_count_ge (unsigned long test) const
{
  unsigned long bytes (0);
  foreach_const (parts_t, parts, it)
    if (((bytes += it->bytes)) >= test)
      return true;
  return false;
}

unsigned long
Article :: get_byte_count () const
{
  unsigned long bytes (0);
  foreach_const (parts_t, parts, it)
    bytes += it->bytes;
  return bytes;
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

void
Article :: set_part_count (unsigned int count)
{
  assert (count > 0);
  parts.resize (count);
}

Article :: Part&
Article :: get_part (unsigned int number)
{
  //std::cerr << LINE_ID << " parts.size() " << parts.size() << " number " << number << std::endl;
  const unsigned int index (number - 1);
  assert (parts.size() > index);
  return parts[index];
}

const Article :: Part&
Article :: get_part (unsigned int number) const
{
  //std::cerr << LINE_ID << " parts.size() " << parts.size() << " number " << number << std::endl;
  const unsigned int index (number - 1);
  assert (parts.size() > index);
  return parts[index];
}

/* Message-IDs in multipart articles are usually nearly identical, like this:
**
**   <JIudnQRwg-iopJbYnZ2dnUVZ_v-dnZ2d@giganews.com>
**   <JIudnQdwg-ihpJbYnZ2dnUVZ_v-dnZ2d@giganews.com>
**   <JIudnQZwg-jepJbYnZ2dnUVZ_v-dnZ2d@giganews.com>
**   <JIudnQFwg-jXpJbYnZ2dnUVZ_v-dnZ2d@giganews.com>
**   <JIudnQBwg-jMpJbYnZ2dnUVZ_v-dnZ2d@giganews.com>
**   <JIudnQNwg-jFpJbYnZ2dnUVZ_v-dnZ2d@giganews.com>
**
** In large newsgroups, _many_ megs can be saved by stripping out common text.
** We assign Article::Part's Message-ID by passing in its real Message-ID and
** a reference key (which currently is always the owner Article's message_id).
** The identical chars at the beginning (b) and end (e) of the two are counted.
** b and e have an upper bound of UCHAR_MAX (255).
** Article::Part::folded_message_id's first byte holds 'b', the second holds 'e',
** and the remainder is a zero-terminated string with the unique middle characters.
*/

void
Article :: Part :: set_message_id (const Quark& key_mid, const StringView& mid)
{
  const StringView key (key_mid.to_view());

  size_t b, e;
  for (b=0; b<UCHAR_MAX && b<key.len && b<mid.len; ++b)
    if (key.str[b] != mid.str[b])
      break;
  for (e=0; e<UCHAR_MAX && b+e<key.len && b+e<mid.len; ++e)
    if (key.str[key.len-1-e] != mid.str[mid.len-1-e])
      break;

  const size_t n_kept (mid.len - e - b);
  char *str(g_new(char,1+n_kept+1+1));
  str[0] = (char) b;
  str[1] = (char) e;
  memcpy (str+2, mid.str+b, n_kept);
  str[1+n_kept+1] = '\0';

  g_free (packed_message_id);
  packed_message_id = str;

  // check our work
  //assert (mid == get_message_id(key));
}

std::string
Article :: Part :: get_message_id (const Quark& key_mid) const
{
  std::string setme;

  if (packed_message_id)
  {
    const StringView key (key_mid.to_view());
    const char * pch (packed_message_id);
    const int b ((unsigned char) *pch++);
    const int e ((unsigned char) *pch++);
    setme.append (key.str, 0, b);
    setme.append (pch);
    setme.append (key.str + key.len - e, e);
  }

  return setme;
}

namespace
{
  char* clone_packed_mid (const char * mid)
  {
    // 2 to pick up b and e; 1 to pick up the '\0'
    return mid ? (char*) g_memdup (mid, 2+strlen(mid+2)+1) : 0;
  }
}

void
Article :: Part :: clear ()
{
  bytes = 0;
  g_free (packed_message_id);
  packed_message_id = 0;
}

void
Article :: Part :: swap (Part& that)
{
  std::swap (bytes, that.bytes);
  std::swap (packed_message_id, that.packed_message_id);
}

Article :: Part :: Part (const Part& that):
  packed_message_id (clone_packed_mid (that.packed_message_id)),
  bytes (that.bytes)
{
}

Article :: Part&
Article :: Part :: operator= (const Part& that)
{
  bytes = that.bytes;
  g_free (packed_message_id);
  packed_message_id = clone_packed_mid (that.packed_message_id);
  return *this;
}

/***
****
***/

Article :: mid_sequence_t
Article :: get_part_mids () const
{
  mid_sequence_t mids;
  mids.reserve (parts.size());

  const Quark& key (message_id);
  foreach_const (parts_t, parts, it) {
    const Part& p (*it);
    if (!p.empty())
      mids.push_back (p.get_message_id(key));
  }

  return mids;
}

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
