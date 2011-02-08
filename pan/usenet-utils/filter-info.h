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

#ifndef __Filter_Info_h__
#define __Filter_Info_h__

#include <deque>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/general/text-match.h>

namespace pan
{
  /**
   * Interface class describing a filter that can be applied to a set of articles.
   * @ingroup usenet_utils 
   */
  class FilterInfo
  {
    public:

      /** The different type of filters we support. */
      enum Type {
        TYPE_ERR,
        AGGREGATE_AND,
        AGGREGATE_OR,
        IS_BINARY,
        IS_CACHED,
        IS_POSTED_BY_ME,
        IS_UNREAD,
        BYTE_COUNT_GE,
        CROSSPOST_COUNT_GE,
        DAYS_OLD_GE,
        LINE_COUNT_GE,
        SCORE_GE,
        TEXT
      };

    public:
      bool empty() const { return _type == TYPE_ERR; }
      FilterInfo () { clear(); }
      virtual ~FilterInfo () { }

    public:

      /** Defines what type of filter this is. */
      Type _type;

      /** When `_type' is *_GE, we're comparing to see if the article's
          field is greater than or equal to this value. */
      long _ge;

      /** When `_type' is TEXT, this is the header we're testing */
      Quark _header;

      /** When `_type' is TEXT, this is how we're testing the header */
      TextMatch _text;

      /** Convenience typedef. */
      typedef std::deque<FilterInfo> aggregates_t;

      /** When `_type' is AGGREGATE_OR or AGGREGATE_AND,
          these are the filters being or'ed or and'ed together. */
      aggregates_t _aggregates;

      /** When this is true, the results of the test should be negated. */
      bool _negate;

      /** When this is true the test needs the article body. */
      bool _needs_body;

    private:
      void set_type_is (Type type);
      void set_type_ge (Type type, unsigned long ge);
      void set_type_le (Type type, unsigned long le);

    public:
      void clear ();
      void set_type_aggregate_and ();
      void set_type_aggregate_or ();
      void set_type_binary ();
      void set_type_byte_count_ge (unsigned long ge);
      void set_type_cached ();
      void set_type_crosspost_count_ge (unsigned long ge);
      void set_type_days_old_ge (unsigned long ge);
      void set_type_days_old_le (unsigned long ge);
      void set_type_line_count_ge (unsigned long ge);
      void set_type_score_ge (unsigned long ge);
      void set_type_score_le (unsigned long le);
      void set_type_is_unread ();
      void set_type_posted_by_me ();
      void set_type_text (const Quark& header,const TextMatch::Description&);
      void set_negate (bool b) { _negate = b; }

    public:
      std::string describe () const;
  };
}

#endif
