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

void
RulesFilter :: finalize (Data& data)
{
  data.delete_articles (_delete);
  _delete.clear();

  const std::vector<const Article*> tmp (_mark_read.begin(), _mark_read.end());
  data.mark_read ((const Article**)&tmp.front(), tmp.size());
  _mark_read.clear();

  const std::vector<const Article*> tmp2 ( _cached.begin(),  _cached.end());
  _cached.clear();

  const std::vector<const Article*> tmp3 (_downloaded.begin(), _downloaded.end());
  _downloaded.clear();
}

bool
RulesFilter :: test_article ( Data        & data,
                              RulesInfo   & rules,
                              const Quark & group,
                              Article     & article)
{

  bool pass (article.score >= rules._lb && article.score <= rules._hb);
  if (rules._hb >= 9999 && article.score >= rules._hb) pass = true;
  if (rules._lb <= -9999 && article.score <= rules._lb) pass = true;

  switch (rules._type)
  {
    case RulesInfo::AGGREGATE__AND:
      pass = true;
      foreach (RulesInfo::aggregates_t, rules._aggregates, it)
        test_article (data, *it, group, article);
      break;

    case RulesInfo::AGGREGATE__OR:
      if (rules._aggregates.empty())
        pass = true;
      else {
        pass = false;
        foreach (RulesInfo::aggregates_t, rules._aggregates, it) {
          if (test_article (data, *it, group, article)) {
            pass = true;
            break;
          }
        }
      }
      break;

    case RulesInfo::MARK_READ:

      if (pass)
        _mark_read.insert(&article);
      break;

    case RulesInfo::AUTOCACHE:
      if (pass)
      {
        _cached.insert (&article);
        if (_auto_cache_mark_read) _mark_read.insert(&article);
      }
      break;

    case RulesInfo::AUTODOWNLOAD:
      if (pass)
      {
        _downloaded.insert (&article);
        if (_auto_dl_mark_read) _mark_read.insert(&article);
      }
      break;

    case RulesInfo::DELETE_ARTICLE:
      if (pass)
      {
         _delete.insert (&article);
         if (_auto_delete_mark_read) _mark_read.insert(&article);
      }
      break;

    default:
//     debug("error : unknown rules type "<<rules._type);
     return true;
  }

  return pass;
}

