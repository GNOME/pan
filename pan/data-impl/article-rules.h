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

#ifndef __RulesFilter_h__
#define __RulesFilter_h__

#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/general/quark.h>
#include <pan/tasks/queue.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>

namespace pan {
/**
 * @ingroup data_impl
 */
class ArticleRules
{

  public:
    ArticleRules(bool cache, bool dl) :
      _auto_cache_mark_read(cache),
      _auto_dl_mark_read(dl)
    {
    }

    bool apply_rules(Data &data,
                      RulesInfo &rules,
                      Quark const &group,
                      Article &article);

  private:
    std::set<Article const *> _mark_read;
    std::set<Article const *> _delete;
    bool _auto_cache_mark_read, _auto_dl_mark_read;

  public:
    std::set<Article const *> _cached;
    std::set<Article const *> _downloaded;
    void finalize(Data &data);
};
} // namespace pan

#endif
