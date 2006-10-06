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

#include <vector>
#include <ctime>
#include <pan/general/sorted-vector.h>
#include <pan/general/quark.h>
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

      /**
       * The fields of a single part of a multipart Article.
       * For other fields, refer to the parent Article object.
       */
      class Part
      {
        private:
          char * packed_message_id;

        public:
          unsigned long bytes;

        public:
          void set_message_id (const Quark& key, const StringView& mid);
          std::string get_message_id (const Quark& key) const;

          Part(): packed_message_id(0), bytes(0) {}
          ~Part() { clear(); }

          bool empty () const { return !packed_message_id; }
          void swap (Part&);
          void clear ();

          Part (const Part&);
          Part& operator= (const Part&);
      };

      typedef std::vector<Part> parts_t;

      Part& get_part (unsigned int part_number);
      const Part& get_part (unsigned int part_number) const;
      void set_part_count (unsigned int);
      typedef std::vector<Quark> mid_sequence_t;
      mid_sequence_t get_part_mids () const;

    public:
      Quark message_id;
      Quark author;
      Quark subject;
      static bool has_reply_leader (const StringView&);

    public:
      enum PartState { SINGLE, INCOMPLETE, COMPLETE };
      PartState get_part_state () const;
      unsigned int get_crosspost_count () const;
      unsigned long get_line_count () const { return lines; }
      unsigned long get_byte_count () const;
      bool is_line_count_ge (size_t test) const { return lines >= test; }
      bool is_byte_count_ge (unsigned long) const;
      unsigned int get_part_count () const { return parts.size(); }

      time_t time_posted;
      unsigned long lines;
      Xref xref;
      int score;
      parts_t parts;

    public:
      Article (): time_posted(0), lines(0), score(0), is_binary(false) {}
      ~Article () {}
      void clear ();

    public:
      bool is_binary;
  };
}

#endif
