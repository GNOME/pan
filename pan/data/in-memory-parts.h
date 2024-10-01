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

#ifndef __InMemory_Parts_h__
#define __InMemory_Parts_h__

#include <ctime>
#include <pan/data/xref.h>
#include <pan/general/quark.h>
#include <pan/general/sorted-vector.h>
#include <stdint.h>
#include <vector>

namespace pan {
class InMemoryPartBatch;

/**
 * Represents a collection of a multipart post's parts.
 *
 * Parts are by far the biggest memory hog in large groups,
 * so their layout has been tweaked to squeeze out extra bytes.
 * This makes the code fairly inelegant, so the low-level bits
 * have been encapsulated in this class.
 *
 * @see Article
 * @see InMemoryPartBatch
 * @ingroup data
 */
class InMemoryParts
{
  public:
    typedef uint16_t number_t;
    typedef uint32_t mid_offset_t;
    typedef uint32_t bytes_t;

  private:
    struct Part
    {
        mid_offset_t mid_offset;
        number_t number;
        bytes_t bytes;

        bool operator<(Part const &that) const
        {
          return number < that.number;
        }
    };

    number_t n_parts_total;
    bytes_t part_mid_buf_len;
    typedef std::vector<Part> part_v;
    part_v parts;
    char *part_mid_buf;
    void unpack_message_id(std::string &setme,
                           Part const *p,
                           Quark const &reference_mid) const;

  public:
    class const_iterator
    {
        Quark const &reference_mid;
        InMemoryParts const &parts;
        mutable std::string midbuf;
        int n;

        Part const &get_part() const
        {
          return parts.parts[n];
        }

      public:
        const_iterator(Quark const &q, InMemoryParts const &p, int pos = 0) :
          reference_mid(q),
          parts(p),
          n(pos)
        {
        }

        bool operator==(const_iterator const &that) const
        {
          return (n == that.n) && (&parts == &that.parts);
        }

        bool operator!=(const_iterator const &that) const
        {
          return ! (*this == that);
        }

        const_iterator &operator++()
        {
          ++n;
          return *this;
        }

        const_iterator operator++(int)
        {
          const_iterator tmp = *this;
          ++*this;
          return tmp;
        }

        bytes_t bytes() const
        {
          return get_part().bytes;
        }

        number_t number() const
        {
          return get_part().number;
        }

        std::string const &mid() const
        {
          parts.unpack_message_id(midbuf, &(get_part()), reference_mid);
          return midbuf;
        }
    };

    const_iterator begin(Quark const &q) const
    {
      return const_iterator(q, *this, 0);
    }

    const_iterator end(Quark const &q) const
    {
      return const_iterator(q, *this, parts.size());
    }

  public:
    InMemoryParts();

    ~InMemoryParts()
    {
      clear();
    }

    InMemoryParts(InMemoryParts const &that);
    InMemoryParts &operator=(InMemoryParts const &);

  public:
    void set_part_count(number_t num)
    {
      n_parts_total = num;
      parts.reserve(num);
    }

    number_t get_total_part_count() const
    {
      return n_parts_total;
    }

    number_t get_found_part_count() const
    {
      return parts.size();
    }

    void clear();
    bool add_part(number_t num,
                  StringView const &mid,
                  bytes_t bytes,
                  Quark const &reference_mid);
    void set_parts(InMemoryPartBatch const &parts);

  public:
    bool get_part_info(number_t num,
                       std::string &mid,
                       bytes_t &bytes,
                       Quark const &reference_mid) const;
};

  /**
   * Batches together parts of a multipart post for
   * efficient adding to a InMemoryParts object.
   *
   * @see InMemoryParts
   * @ingroup data
   */
  class InMemoryPartBatch
  {
    public:
      typedef InMemoryParts::number_t number_t;
      typedef InMemoryParts::bytes_t bytes_t;

    private:

      friend class InMemoryParts;
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

      InMemoryPartBatch();
      explicit InMemoryPartBatch(Quark const &mid, number_t n_parts = 0);

      void init (const Quark& mid, number_t n_parts=0);
      void add_part (number_t num, const StringView& mid, bytes_t bytes);
      void sort ();
  };
}

#endif
