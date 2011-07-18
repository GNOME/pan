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

#include <config.h>
#include <cassert>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/data/data.h>
#include <gmime/gmime.h>
#include <glib/gprintf.h>
#include "rules-filter.h"

using namespace pan;

bool
RulesFilter :: test_article (const Data        & data,
                             const RulesInfo   & rules,
                             const Quark       & group,
                             const Article     & article) const
{
  bool pass (false);
  const ArticleCache& cache(data.get_cache());

  switch (rules._type)
  {
    case RulesInfo::AGGREGATE_AND:
      pass = true;
      foreach_const (RulesInfo::aggregates_t, rules._aggregates, it) {
        // assume test passes if test needs body but article not cached
        if (!it->_needs_body || cache.contains(article.message_id) )
          if (!test_article (data, *it, group, article)) {
            pass = false;
            break;
          }
      }
      break;

    case RulesInfo::AGGREGATE_OR:
      if (rules._aggregates.empty())
        pass = true;
      else {
        pass = false;
        foreach_const (RulesInfo::aggregates_t, rules._aggregates, it) {
          // assume test fails if test needs body but article not cached
          if (!it->_needs_body || cache.contains(article.message_id) )
            if (test_article (data, *it, group, article)) {
              pass = true;
              break;
            }
        }
      }
      break;

    case RulesInfo::MARK_READ:
      pass = article.score == Article::COMPLETE;
    break;
  }

  return pass;
}

