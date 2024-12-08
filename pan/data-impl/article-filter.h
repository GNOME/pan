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

#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/usenet-utils/scorefile.h>

namespace pan {
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
    Quark const subject;
    Quark const from;
    Quark const xref;
    Quark const references;
    Quark const newsgroups;
    Quark const message_Id;
    Quark const message_ID;

    StringView const get_header(Article const &a,
                                Quark const &header_name) const;

  public:
    ArticleFilter() :
      subject("Subject"),
      from("From"),
      xref("Xref"),
      references("References"),
      newsgroups("Newsgroups"),
      message_Id("Message-Id"),
      message_ID("Message-ID")
    {
    }

    typedef std::vector<Article const *> articles_t;

    void test_articles(Data const &data,
                       FilterInfo const &criteria,
                       Quark const &group,
                       articles_t const &in,
                       articles_t &setme_pass,
                       articles_t &setme_fail) const;

    bool test_article(Data const &data,
                      FilterInfo const &criteria,
                      Quark const &group,
                      Article const &article) const;

    typedef std::vector<Scorefile::Section const *> sections_t;

    int score_article(Data const &data,
                      sections_t const &score,
                      Quark const &group,
                      Article const &article) const;

    void get_article_scores(Data const &data,
                            sections_t const &score,
                            Quark const &group,
                            Article const &article,
                            Scorefile::items_t &setme) const;
};
} // namespace pan

#endif
