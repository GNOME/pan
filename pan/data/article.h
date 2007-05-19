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

#ifndef __Article_h__
#define __Article_h__

#include <ctime>
#include <vector>
#include <pan/general/sorted-vector.h>
#include <pan/general/quark.h>
#include <pan/data/parts.h>
#include <pan/data/xref.h>

namespace pan
{
  /**
   * A Usenet article, either single-part or multipart.
   * 
   * To lessen the memory footprint of large binaries groups,
   * Pan folds multipart posts into a single Article object.
   * Only minimal information for any one part is kept
   * (message-id, line count, byte count), and the Article object
   * holds the rest.
   *
   * This is a lossy approach: less-important unique fields,
   * such as the part's xref and time-posted, are not needed
   * and so we don't keep them.
   *
   * @ingroup data
   */
  class Article
  {
    public:
      void set_parts (const PartBatch& b) { parts.set_parts(b); }
      bool add_part (Parts::number_t num, const StringView& mid, Parts::bytes_t bytes) { return parts.add_part(num,mid,bytes,message_id); }
      void set_part_count (Parts::number_t num) { parts.set_part_count(num); }
      Parts::number_t get_total_part_count () const { return parts.get_total_part_count(); }
      Parts::number_t get_found_part_count () const { return parts.get_found_part_count(); }
      bool get_part_info (Parts::number_t      num,
                          std::string & mid,
                          Parts::bytes_t     & bytes) const { return parts.get_part_info(num,mid,bytes,message_id); }

      typedef Parts::const_iterator part_iterator;
      part_iterator pbegin() const { return parts.begin(message_id); }
      part_iterator pend() const { return parts.end(message_id); }

      typedef std::vector<Quark> mid_sequence_t;
      mid_sequence_t get_part_mids () const;
      enum PartState { SINGLE, INCOMPLETE, COMPLETE };
      PartState get_part_state () const;

    public:
      bool is_binary;
      Quark message_id;
      Quark author;
      Quark subject;
      static bool has_reply_leader (const StringView&);

    public:
      unsigned int get_crosspost_count () const;
      unsigned long get_line_count () const { return lines; }
      bool is_line_count_ge (size_t test) const { return lines >= test; }
      unsigned long get_byte_count () const;
      bool is_byte_count_ge (unsigned long test) const;

      unsigned int lines;
      int score;
      time_t time_posted;
      Xref xref;

    public:
      Article (): is_binary(false), lines(0), score(0), time_posted(0) {}
      void clear ();

    private:
      Parts parts;

  };
}

#endif
