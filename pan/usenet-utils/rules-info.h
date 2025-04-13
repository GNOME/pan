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

#ifndef __Rules_Info_h__
#define __Rules_Info_h__

#include <deque>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/general/text-match.h>

/**
 * Pre-declaring swap(...) is a bit involved, given the use
 * of the namespace pan and it needs to be outside.
 */
namespace pan {
  class RulesInfo;
};

void swap(pan::RulesInfo &first, pan::RulesInfo &second);

namespace pan
{
  /**
   * Interface class describing a filter that can be applied to a set of articles.
   * @ingroup usenet_utils
   * RulesInfo is used with a top level RuleInfo with AGGREGATE_RULES type.
   * It contains a set of rules of type MARK_READ, AUTOCACHE, AUTODOWNLOAD DELETE_ARTICLE
   * This construct is not used in a recursive way.
   */
  class RulesInfo
  {
    public:

      /** The different type of filters we support. */
      enum RulesType {
        TYPE__ERR,
        AGGREGATE_RULES, // all rules are applied to each article
        MARK_READ,
        AUTOCACHE,
        AUTODOWNLOAD,
        DELETE_ARTICLE
      };

      /** Defines what type of filter this is. */
      RulesType _type;

      bool empty() const { return _type == TYPE__ERR; }
      RulesInfo () { clear(); }
      RulesInfo (const RulesInfo &that);
      friend void ::swap (RulesInfo &first, RulesInfo &second);
      RulesInfo &operator = (RulesInfo other);
      virtual ~RulesInfo ();

      /** Convenience typedef. */
      typedef std::deque<RulesInfo *> aggregatesp_t;

      /** When `_type' is AGGREGATE_RULES
          the aggregated rules are applied one by one. */
      aggregatesp_t _aggregates;

    private:
      void set_type_is (RulesType type);
      void set_type_le (RulesType type, unsigned long le);
      void set_type_ge (RulesType type, unsigned long ge);
      void set_type_bounds (RulesType type, int low, int high);

    public:

      unsigned long _ge;
      int _lb, _hb;

      void clear ();
      void set_type_aggregate ();
      void set_type_mark_read_b (int lb, int hb);
      void set_type_autocache_b (int lb, int hb);
      void set_type_dl_b (int lb, int hb);
      void set_type_delete_b (int lb, int hb);
  };
}

#endif
