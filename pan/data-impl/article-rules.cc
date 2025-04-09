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

#include "article-rules.h"
#include <cassert>
#include <config.h>
#include <glib/gprintf.h>
#include <gmime/gmime.h>
#include <pan/data/data.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>

using namespace pan;

void ArticleRules ::finalize(Data &data)
{

  data.delete_articles(_delete);
  _delete.clear();

  std::vector<Article const *> const tmp(_mark_read.begin(), _mark_read.end());
  if (! tmp.empty())
  {
    data.mark_read((Article const **)&tmp.front(), tmp.size());
  }
  _mark_read.clear();

  std::vector<Article const *> const tmp2(_cached.begin(), _cached.end());
  _cached.clear();

  std::vector<Article const *> const tmp3(_downloaded.begin(),
                                          _downloaded.end());
  _downloaded.clear();
}

bool ArticleRules ::apply_rules(Data &data,
                                RulesInfo &rules,
                                Quark const &group,
                                Article &article)
{

  // check if one of the rules below must be applied
  bool apply(article.score >= rules._lb && article.score <= rules._hb);

  switch (rules._type)
  {
    case RulesInfo::AGGREGATE_RULES:
      apply = true;
      foreach (RulesInfo::aggregatesp_t, rules._aggregates, it)
      {
        apply_rules(data, **it, group, article);
      }
      break;

    case RulesInfo::MARK_READ:

      if (apply)
      {
        _mark_read.insert(&article);
      }
      break;

    case RulesInfo::AUTOCACHE:
      if (apply)
      {
        _cached.insert(&article);
        if (_auto_cache_mark_read)
        {
          _mark_read.insert(&article);
        }
      }
      break;

    case RulesInfo::AUTODOWNLOAD:
      if (apply)
      {
        _downloaded.insert(&article);
        if (_auto_dl_mark_read)
        {
          _mark_read.insert(&article);
        }
      }
      break;

    case RulesInfo::DELETE_ARTICLE:
      if (apply)
      {
        _delete.insert(&article);
      }
      break;

    default:
      //     pan_debug("error : unknown rules type "<<rules._type);
      return true;
  }

  return apply;
}
