/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007  Charles Kerr <charles@rebelbase.com>
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

#include "parts.h"

#include <config.h>
#ifdef DEBUG
#include <cassert>
#include <string>
#endif
#include <algorithm>
#include <pan/general/string-view.h>

#undef DEBUG

namespace pan {
/***
****
***/

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
namespace
{
  /**
   * pack_message_id is broken into two steps so that
   * the client code can ensure the buffer is big enough
   * before we write the mid to it.
   */
  struct Packer
  {
    int size() const { return midlen+3; } // three for for b, e, and '\0'
    uint8_t b, e;
    const char * mid;
    size_t midlen;
    Packer(): b(0), e(0), mid(nullptr), midlen(0) {}
    void pack (char* buf) const {
      *buf++ = b;
      *buf++ = e;
      memcpy (buf, mid, midlen);
      buf[midlen] = '\0';
    }
  };

  void
  pack_message_id (Packer            & setme,
                   const StringView  & mid,
                   const Quark       & key_mid)
  {
    uint8_t b=0, e=0;
    const char *k, *m;
    const StringView& key (key_mid.to_view());
    const int shorter = (int) std::min (key.len, mid.len);

    const int bmax = std::min (shorter, (int)UCHAR_MAX);
    k = &key.front();
    m = &mid.front();
    for (; b!=bmax; ++b)
      if (*k++ != *m++)
        break;

    const int emax = std::min (shorter-b, (int)UCHAR_MAX);
    k = &key.back();
    m = &mid.back();
    for (; e!=emax; ++e)
      if (*k-- != *m--)
        break;

    setme.b = b;
    setme.e = e;
    setme.mid = mid.str + b;
    setme.midlen = mid.len - e - b;
  }

  size_t
  unpack_message_id (std::string       & setme,
                     const StringView  & mid,
                     const Quark       & key_mid)
  {
    if (mid.len < 2)
    {
      setme.clear ();
      return 0;
    }

    const StringView& key (key_mid.to_view());
    const uint8_t b (mid.str[0]);
    const uint8_t e (mid.str[1]);
    const size_t m (mid.len - 2);
    const size_t n = b + e + m;
    setme.resize (n);
    char * pch = &setme[0];
    memcpy (pch, key.str, b); pch += b;
    memcpy (pch, mid.str+2, m); pch += m;
    memcpy (pch, key.str+key.len-e, e);
    return n;
  }
}

/***
****
***/
Parts :: Parts ():
  n_parts_total (0),
  part_mid_buf_len (0),
  parts (0),
  part_mid_buf (nullptr)
{
}

void
Parts :: clear ()
{
  part_v tmp;
  parts.swap(tmp);

  delete [] part_mid_buf;
  part_mid_buf = nullptr;

  part_mid_buf_len = 0;
  n_parts_total = 0;
}

Parts :: Parts (const Parts& that):
  n_parts_total (0),
  part_mid_buf_len (0),
  parts (0),
  part_mid_buf (nullptr)
{
  *this = that;
}

Parts&
Parts :: operator= (const Parts& that)
{
  if (this != &that)
  {
    clear ();

    n_parts_total = that.n_parts_total;
    part_mid_buf_len = that.part_mid_buf_len;
    parts.reserve( n_parts_total );
    parts = that.parts;
    assert( parts.capacity() == n_parts_total );
    part_mid_buf = new char [part_mid_buf_len];
    memcpy (part_mid_buf, that.part_mid_buf, part_mid_buf_len);
  }

  return *this;
}

/***
****
***/

void
Parts :: unpack_message_id (std::string  & setme,
                            const Part   * part,
                            const Quark  & reference_mid) const
{
  StringView v;
  v.str = part_mid_buf + part->mid_offset;
  v.len = 2 + strlen (v.str+2);
  ::pan::unpack_message_id (setme, v, reference_mid);
}

bool
Parts :: get_part_info (number_t        part_number,
                        std::string   & setme_message_id,
                        bytes_t       & setme_byte_count,
                        const Quark   & reference_mid) const
{
  Part findme;
  findme.number = part_number;
  part_v::const_iterator p = std::lower_bound (parts.begin(), parts.end(), findme);
  if ((p == parts.end()) || (p->number != part_number))
    return false;

  unpack_message_id (setme_message_id, &(*p), reference_mid);
  setme_byte_count = p->bytes;
  return true;
}

/***
****
***/

void
Parts :: set_parts (const PartBatch& p)
{
  clear ();

  unsigned int n_parts_found = p.n_parts_found;
  n_parts_total = p.n_parts_total;
  part_mid_buf_len = p.packed_mids_len;
  parts.reserve(n_parts_total);

  char * pch = part_mid_buf = new char [part_mid_buf_len];
  Part part;
  PartBatch::parts_t::const_iterator in = p.parts.begin();
  for (size_t i=0; i<n_parts_found; ++i, ++in) {
    part.number = in->number;
    part.bytes = in->bytes;
    part.mid_offset = pch - part_mid_buf;
    parts.push_back(part);
    memcpy (pch, in->packed_mid, in->len_used);
    pch += in->len_used;
  }

  assert (pch == part_mid_buf + part_mid_buf_len);
}

bool
Parts :: add_part (number_t            num,
                   const StringView  & mid,
                   bytes_t             bytes,
                   const Quark       & reference_mid)
{
  Part findme;
  findme.number = num;
  part_v::iterator p = std::lower_bound (parts.begin(), parts.end(), findme);
  if (p!=parts.end() && p->number == num) // we have it already
    return false;

  findme.bytes = bytes;
  findme.mid_offset = part_mid_buf_len;
  parts.insert(p, findme);

  Packer packer;
  pack_message_id (packer, mid, reference_mid);
  const size_t midlen = packer.size ();
  char * mbuf = new char [part_mid_buf_len + midlen];
  memcpy (mbuf, part_mid_buf, part_mid_buf_len);
  packer.pack (mbuf + part_mid_buf_len);
  delete [] part_mid_buf;
  part_mid_buf = mbuf;
  part_mid_buf_len += midlen;

#ifdef DEBUG
  std::string test_mid;
  bytes_t test_bytes;
  assert (get_part_info (num, test_mid, test_bytes, reference_mid));
  assert (test_bytes == bytes);
  assert (mid == test_mid);
#endif

  return true; // yes, we added it
}

/****
*****
****/

PartBatch::PartBatch() :
  n_parts_found(0),
  n_parts_total(0),
  packed_mids_len(0)
{
}

PartBatch::PartBatch(Quark const &mid, number_t parts_total) :
  reference_mid(mid),
  n_parts_found(0),
  n_parts_total(parts_total),
  packed_mids_len(0)
{
  parts.reserve(n_parts_total);
}

void
PartBatch :: init (const Quark  & mid,
                   number_t       parts_total)
{
  reference_mid = mid;
  packed_mids_len = 0;
  n_parts_total = parts_total;
  n_parts_found = 0; // they haven't been added yet

  parts.clear();
  parts.reserve(n_parts_total);
}

void
PartBatch :: add_part (number_t            number,
                       const StringView  & mid,
                       bytes_t             bytes)
{

  Packer packer;
  pack_message_id (packer, mid, reference_mid);
  Part p(number,bytes,packer.size());
  packer.pack (p.packed_mid);
  packed_mids_len += p.len_used;

#ifdef DEBUG
  // check our work
  std::string tmp;
  ::unpack_message_id (tmp, StringView(p.packed_mid,p.len_used-1), reference_mid);
  assert (mid == tmp);
#endif

  if (n_parts_total < ++n_parts_found)
      n_parts_total = n_parts_found;
  parts.push_back(p);
}

PartBatch :: Part&
PartBatch :: Part :: operator= (const PartBatch :: Part& that)
{
  if (this != &that)
  {
    number =  that.number;
    bytes =  that.bytes;
    len_used = that.len_used;
    delete [] packed_mid;
    packed_mid = new char [len_used];
    memcpy (packed_mid, that.packed_mid, len_used);
  }
  return *this;
}

PartBatch :: Part :: Part (const PartBatch::Part& that):
  number (that.number),
  bytes (that.bytes),
  len_used (that.len_used),
  packed_mid (new char [len_used])
{
  memcpy (packed_mid, that.packed_mid, len_used);
}

PartBatch :: Part :: Part (number_t n, bytes_t b, size_t l):
    number(n),
    bytes(b),
    len_used(l),
    packed_mid(new char [len_used])
{
}

void
PartBatch :: sort ()
{
  std::sort (parts.begin (), parts.end ());
}

}
