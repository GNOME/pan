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

#ifndef __ArticleFilter_h__
#define __ArticleFilter_h__

#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>
#include <pan/data/article.h>
#include <pan/data/data.h>

namespace pan
{
  /**
   * This private class should only be used by data-impl classes.
   *
   * It's used for implementing article filters as described by
   * FilterInfo in the 'backend interfaces' module.
   *
   * @ingroup data_impl
   */
  class ArticleFilter
  {
    private:

      const Quark subject;
      const Quark from;
      const Quark xref;
      const Quark references;
      const Quark newsgroups;
      const Quark message_Id;
      const Quark message_ID;

      const StringView get_header (const Article& a, const Quark& header_name) const;

    public:

      ArticleFilter():
        subject("Subject"),
        from("From"),
        xref("Xref"),
        references("References"),
        newsgroups("Newsgroups"),
        message_Id("Message-Id"),
        message_ID("Message-ID")
      {
      }

      typedef std::vector<const Article*> articles_t;

      void test_articles (const Data& data,
                          const FilterInfo& criteria,
                          const Quark& group,
                          const articles_t& in,
                          articles_t& setme_pass,
                          articles_t& setme_fail) const;

      bool test_article (const Data& data,
                         const FilterInfo& criteria,
                         const Quark& group,
                         const Article& article) const;

      typedef std::vector<const Scorefile::Section*> sections_t;

      int score_article (const Data& data,
                         const sections_t& score,
                         const Quark& group,
                         const Article& article) const;

      void get_article_scores (const Data          & data,
                               const sections_t    & score,
                               const Quark         & group,
                               const Article       & article,
                               Scorefile::items_t  & setme) const;
  };
}

#endif
