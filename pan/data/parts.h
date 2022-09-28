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

#ifndef __Parts_h__
#define __Parts_h__

#include <ctime>
#include <vector>
#include <stdint.h>
#include <pan/general/sorted-vector.h>
#include <pan/general/quark.h>
#include <pan/data/xref.h>

namespace pan
{
  class PartBatch;

  /**
   * Represents a collection of a multipart post's parts.
   *
   * Parts are by far the biggest memory hog in large groups,
   * so their layout has been tweaked to squeeze out extra bytes.
   * This makes the code fairly inelegant, so the low-level bits
   * have been encapsulated in this class.
   *
   * @see Article
   * @see PartBatch
   * @ingroup data
   */
  class Parts
  {
    public:
      typedef uint16_t number_t;
      typedef uint32_t mid_offset_t;
      typedef uint32_t bytes_t;

    private:
      struct Part {
        mid_offset_t mid_offset;
        number_t number;
        bytes_t bytes;
        bool operator< (const Part& that) const { return number < that.number; }
      };
      number_t n_parts_total;
      bytes_t part_mid_buf_len;
      typedef std::vector<Part> part_v;
      part_v parts;
      char * part_mid_buf;
      void unpack_message_id (std::string  & setme,
                              const Part   * p,
                              const Quark  & reference_mid) const;

    public:
      class const_iterator {
          const Quark& reference_mid;
          const Parts& parts;
          mutable std::string midbuf;
          int n;
          const Part& get_part() const { return parts.parts[n]; }
        public:
          const_iterator (const Quark& q,
                          const Parts& p,
                          int pos=0): reference_mid(q), parts(p), n(pos) {}
          bool operator== (const const_iterator& that)
                           const { return (n==that.n) && (&parts==&that.parts); }
          bool operator!= (const const_iterator& that)
                           const { return !(*this == that); }
          const_iterator& operator++() { ++n; return *this; }
          const_iterator operator++(int)
                             { const_iterator tmp=*this; ++*this; return tmp; }
          bytes_t bytes() const { return get_part().bytes; }
          number_t number() const { return get_part().number; }
          const std::string& mid() const {
            parts.unpack_message_id (midbuf, &(get_part()), reference_mid);
            return midbuf;
          }
      };

      const_iterator begin(const Quark& q) const
                                        { return const_iterator(q,*this,0); }
      const_iterator end(const Quark& q) const
                            { return const_iterator(q, *this, parts.size()); }

    public:
      Parts ();
      ~Parts () { clear(); }
      Parts (const Parts& that);
      Parts& operator= (const Parts&);

    public:
      void set_part_count (number_t num) { n_parts_total = num; parts.reserve(num); }
      number_t get_total_part_count () const { return n_parts_total; }
      number_t get_found_part_count () const { return parts.size(); }
      void clear ();
      bool add_part (number_t num,
                     const StringView& mid,
                     bytes_t bytes,
                     const Quark& reference_mid);
      void set_parts (const PartBatch& parts);

    public:
      bool get_part_info (number_t        num,
                          std::string   & mid,
                          bytes_t       & bytes,
                          const Quark   & reference_mid) const;
  };

  /**
   * Batches together parts of a multipart post for
   * efficient adding to a Parts object.
   *
   * @see Parts
   * @ingroup data
   */
  class PartBatch
  {
    public:
      typedef Parts::number_t number_t;
      typedef Parts::bytes_t bytes_t;

    private:

      friend class Parts;
      struct Part {
        number_t number;
        bytes_t bytes;
        size_t len_used;
        char * packed_mid;
        Part(): number(0), bytes(0),
                len_used(0), packed_mid(nullptr) {}
        Part(number_t n, bytes_t b, size_t l);
        ~Part() { delete [] packed_mid; }
        Part (const Part&);
        Part& operator= (const Part&);
        bool operator< (const Part& that) const { return number < that.number; }
      };
      Quark reference_mid;
      number_t n_parts_found;
      number_t n_parts_total;
      typedef std::vector<Part> parts_t;
      parts_t parts;
      size_t packed_mids_len;

    public:

      PartBatch();
      explicit PartBatch(Quark const &mid, number_t n_parts = 0);

      void init (const Quark& mid, number_t n_parts=0);
      void add_part (number_t num, const StringView& mid, bytes_t bytes);
      void sort ();
  };
}

#endif
