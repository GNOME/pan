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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <cassert>
#include <algorithm>
#include <pan/general/debug.h>
#include <pan/general/foreach.h>
#include "article.h"

using namespace pan;

#undef DEBUG

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
    Packer(): b(0), e(0), mid(0), midlen(0) {}
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
    register uint8_t b=0, e=0;
    register const char *k, *m;
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
  n_parts_found (0),
  n_parts_total (0),
  part_mid_buf_len (0),
  parts (0),
  part_mid_buf (0)
{
}

void
Parts :: clear ()
{
  delete [] parts;
  parts = 0;

  delete [] part_mid_buf;
  part_mid_buf = 0;

  part_mid_buf_len = 0;
  n_parts_found = 0;
  n_parts_total = 0;
}

Parts :: Parts (const Parts& that):
  n_parts_found (0),
  n_parts_total (0),
  part_mid_buf_len (0),
  parts (0),
  part_mid_buf (0)
{
  *this = that;
}

Parts&
Parts :: operator= (const Parts& that)
{
  clear ();

  n_parts_found = that.n_parts_found;
  n_parts_total = that.n_parts_total;
  part_mid_buf_len = that.part_mid_buf_len;
  part_mid_buf = new char [part_mid_buf_len];
  memcpy (part_mid_buf, that.part_mid_buf, part_mid_buf_len);

  Part * part = parts = new Part [n_parts_found];
  const Part * that_part = that.parts;
  for (size_t i=0; i<n_parts_found; ++i, ++part, ++that_part) {
    part->number = that_part->number;
    part->bytes = that_part->bytes;
    part->mid_offset = that_part->mid_offset;
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
  ::unpack_message_id (setme, v, reference_mid);
}

bool
Parts :: get_part_info (number_t        part_number,
                        std::string   & setme_message_id,
                        bytes_t       & setme_byte_count,
                        const Quark   & reference_mid) const
{
  Part findme;
  findme.number = part_number;
  Part * p = std::lower_bound (parts, parts+n_parts_found, findme);
  if ((p == parts+n_parts_found) || (p->number != part_number))
    return false;

  unpack_message_id (setme_message_id, p, reference_mid);
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

  n_parts_found = p.n_parts_found;
  n_parts_total = p.n_parts_total;
  part_mid_buf_len = p.packed_mids_len;

  char * pch = part_mid_buf = new char [part_mid_buf_len];
  Part * part = parts = new Part [n_parts_found];
  PartBatch::parts_t::const_iterator in = p.parts.begin();
  for (size_t i=0, n=n_parts_found; i<n; ++i, ++in, ++part) {
    part->number = in->number;
    part->bytes = in->bytes;
    part->mid_offset = pch - part_mid_buf;
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
  Part * p = std::lower_bound (parts, parts+n_parts_found, findme);
  if (p!=parts+n_parts_found && p->number == num) // we have it already
    return false;

  const int insert_index = (p - parts);
  Part * buf = new Part [n_parts_found+1];
  std::copy (parts, parts+insert_index, buf);
  std::copy (parts+insert_index, parts+n_parts_found, buf+insert_index+1);
  delete [] parts;
  parts = buf;
  buf += insert_index;
  buf->number = num;
  buf->bytes = bytes;
  buf->mid_offset = part_mid_buf_len;
  ++n_parts_found;

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

void
PartBatch :: init (const Quark  & mid,
                   number_t       n_parts_total,
                   number_t       n_parts_found)
{
  this->reference_mid = mid;
  this->packed_mids_len = 0;
  this->n_parts_total = n_parts_total;
  this->n_parts_found = 0; // they haven't been added yet

  if (n_parts_found > parts.size())
    parts.resize (n_parts_found);
}

void
PartBatch :: add_part (number_t            number,
                       const StringView  & mid,
                       bytes_t             bytes)
{
  if (n_parts_found >= parts.size())
    parts.resize (n_parts_found+1);

  Part& p = *(&parts.front() + n_parts_found++);
  p.number = number;
  p.bytes = bytes;

  Packer packer;
  pack_message_id (packer, mid, reference_mid);
  p.len_used = packer.size ();
  if (p.len_alloced < p.len_used) {
    delete [] p.packed_mid;
    p.packed_mid = new char [p.len_used];
    p.len_alloced = p.len_used;
  }
  packer.pack (p.packed_mid);
  packed_mids_len += p.len_used;

#ifdef DEBUG
  // check our work
  std::string tmp;
  ::unpack_message_id (tmp, StringView(p.packed_mid,p.len_used-1), reference_mid);
  assert (mid == tmp);
#endif

  if (n_parts_total < n_parts_found)
      n_parts_total = n_parts_found;
}

PartBatch :: Part&
PartBatch :: Part :: operator= (const PartBatch :: Part& that)
{
  number =  that.number;
  bytes =  that.bytes;
  len_used = len_alloced = that.len_used;
  delete [] packed_mid;
  packed_mid = new char [len_used];
  memcpy (packed_mid, that.packed_mid, len_used);
  return *this;
}

PartBatch :: Part :: Part (const PartBatch::Part& that):
  number (that.number),
  bytes (that.bytes),
  len_used (that.len_used),
  len_alloced (that.len_used),
  packed_mid (new char [len_used])
{
  memcpy (packed_mid, that.packed_mid, len_used);
}
