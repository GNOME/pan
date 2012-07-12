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

#ifndef __RulesFilter_h__
#define __RulesFilter_h__

#include <pan/general/quark.h>
#include <pan/tasks/queue.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>
#include <pan/data/article.h>
#include <pan/data/data.h>


namespace pan
{
  /**
   * @ingroup data_impl
   */
  class RulesFilter
  {

    public:

      RulesFilter(bool cache, bool dl, bool del) : _auto_cache_mark_read(cache), _auto_dl_mark_read(dl), _auto_delete_mark_read(del) {  }

      bool test_article (Data        & data,
                         RulesInfo   & rules,
                         const Quark& group,
                         Article& article);

      private:
        std::set<const Article*> _mark_read;
        std::set<const Article*> _delete;
        bool _auto_cache_mark_read, _auto_dl_mark_read, _auto_delete_mark_read;

      public:

        std::set<const Article*> _cached;
        std::set<const Article*> _downloaded;
        void finalize (Data& data) ;
  };
}

#endif
