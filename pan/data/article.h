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

#include "pan/general/string-view.h"
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
    Parts::number_t get_total_part_count() const;

    Parts::number_t get_found_part_count() const;

    void get_missing_part_numbers(std::set<Parts::number_t> &setme) const;

    typedef std::vector<Quark> mid_sequence_t;
    // get list of message ids of all parts
    mid_sequence_t get_part_mids() const;

    enum PartState
    {
      SINGLE,
      INCOMPLETE,
      COMPLETE
    };

    PartState get_part_state() const;
    PartState char_to_state(char const) const;

  public:
    Quark message_id;

  public:
    static bool has_reply_leader(StringView const &);
    unsigned int get_crosspost_count() const;
    void get_crosspost_groups(std::vector<StringView> &setme) const;

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
    int get_score() const;
    void set_score(int) const;
    bool is_binary() const;
    void is_binary(bool) const;

    bool is_in_db_article_table() const;

    std::string get_rebuilt_xref() const;
    std::string get_xrefed_groups() const;

  public:
    Article()
    {
    }

    Article(std::string mid) :
      message_id(mid)
    {
    }

    void clear();

    /* Functions to bookmark an article */
    void toggle_flag();
    bool get_flag() const;
    void set_flag(bool setme);
};
} // namespace pan

#endif
