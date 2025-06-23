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

#ifndef __Article_h__
#define __Article_h__

#include <ctime>
#include <pan/data/parts.h>
#include <pan/data/xref.h>
#include <pan/general/quark.h>
#include <pan/general/sorted-vector.h>
#include <vector>

namespace pan {
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

// used in both huge model and as tmp data structure for other
// operation like encode (presumably to send article). Parts are
// also used this way so this class must stays as is. Reading
// article may rely on another class, like ArticleInDb and
// PartsInDb.
class Article
{
  public:
    void set_parts(PartBatch const &b)
    {
      parts.set_parts(b);
    }

    bool add_part(Parts::number_t num,
                  StringView const &mid,
                  Parts::bytes_t bytes)
    {
      return parts.add_part(num, mid, bytes, message_id);
    }

    void set_part_count(Parts::number_t num)
    {
      parts.set_part_count(num);
    }

    Parts::number_t get_total_part_count() const;

    Parts::number_t get_found_part_count() const;

    bool get_part_info(Parts::number_t num,
                       std::string &mid,
                       Parts::bytes_t &bytes) const;

    typedef Parts::const_iterator part_iterator;

    part_iterator pbegin() const
    {
      return parts.begin(message_id);
    }

    part_iterator pend() const
    {
      return parts.end(message_id);
    }

    typedef std::vector<Quark> mid_sequence_t;
    mid_sequence_t get_part_mids() const;

    enum PartState
    {
      SINGLE,
      INCOMPLETE,
      COMPLETE
    };

    PartState get_part_state() const;

  public:
    Quark message_id;
    int score;
    bool is_binary;
    bool flag;
    static bool has_reply_leader(StringView const &);

  public:
    unsigned int get_crosspost_count() const;

    unsigned long get_line_count() const;

    bool is_line_count_ge(size_t test) const;
    unsigned long get_byte_count() const;
    bool is_byte_count_ge(unsigned long test) const;
    time_t get_time_posted() const;
    void set_time_posted(time_t) const;
    Quark get_author() const;
    void set_author(Quark) const;
    Quark get_subject() const;
    void set_subject(Quark) const;
    Xref xref;

  public:
    Article() :
      score(0),
      is_binary(false),
      flag(false)
    {
    }

    void clear();

    /* Functions to bookmark an article */
    void toggle_flag()
    {
      flag = ! flag;
    }

    bool get_flag() const
    {
      return flag;
    }

    void set_flag(bool setme)
    {
      flag = setme;
    }

  private:
    Parts parts;
};
} // namespace pan

#endif
