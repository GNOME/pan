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

#ifndef __Numbers_h__
#define __Numbers_h__

#include <stdint.h>
#include <vector>
#include <pan/general/string-view.h>

class string;

namespace pan
{
  /**
   * Builds a range of numbers.  Used for reading/writing newsrc strings.
   *
   * @ingroup usenet_utils
   */
  class Numbers
  {
    public:

      /** Simple two-field struct defining a range. */
      struct Range {
        uint64_t low;
        uint64_t high;
        bool operator ==(const Range& that) const {
          return low==that.low && high==that.high;
        }
        Range (): low(0), high(0) {}
        Range (uint64_t l, uint64_t h): low(l<h?l:h), high(h>l?h:l) {}
        bool contains (uint64_t point) const {
          return low<=point && point<=high;
        }
        bool contains (const Range& r) const {
          return low<=r.low && r.high<=high;
        }
        int compare (uint64_t point) const {
          if (point < low) return -1;
          if (point > high) return 1;
          return 0;
        }
        bool operator< (uint64_t point) const {
          return high < point;
        }
      };

      typedef std::vector<Range> ranges_t;
      
    private:
      ranges_t _marked;
      uint64_t mark_range (const Range&);
      uint64_t unmark_range (const Range&);

    public:
      bool operator== (const Numbers& that) const {
        return _marked == that._marked;
      }
      Numbers () { _marked.reserve(16);}
      ~Numbers () { }

    public:

      /**
       * Add or remove the specified number from the set.
       * numbers outside the range [range_low...range_high] are ignored.
       *
       * @param number the number to add or remove.
       * @param add true if we're adding the number, false if removing
       * @return the number's previous state in the set.
       */
      bool mark_one (uint64_t number, bool add=true);

      /**
       * Add or remove the specified range of numbers from the set.
       * numbers outside the range [range_low...range_high] are ignored.
       *
       * @param low the lower bound, inclusive, of the range we're changing.
       * @param high the upper bound, inclusive, of the range we're changing.
       * @param add true if we're adding the numbers, false if removing
       * @return the quantity of numbers whose presence in the set changed.
       */
      uint64_t mark_range (uint64_t low, uint64_t high, bool add=true);

      /**
       * Mark numbers from a text string in to_str() and from_str() fromat.
       * @param numbers ascii-formatted string of numbers to mark.
       * @param add true if we're adding the numbers, false if removing
       */
      void mark_str (const StringView& str, bool add=true);

      /**
       * @return true if the number is in this set, false otherwise
       */
      bool is_marked (const uint64_t) const;

      void clear ();

    public:

      bool from_string (const char *);

      std::string to_string () const;
      void to_string (std::string& setme) const;
      operator std::string () const { return to_string(); }

    public:

      void clip (const uint64_t low, const uint64_t high);
  };
}

#endif /* __Numbers_h__ */
