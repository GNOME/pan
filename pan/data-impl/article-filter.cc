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

#include <cassert>
#include <config.h>
#include <gmime/gmime.h>
#include <log4cxx/logger.h>
#include <pan/data/data.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>

// #include <glib/gprintf.h>
#include <glib.h>

#include "article-filter.h"
#include "pan/general/log4cxx.h"
#include "pan/general/string-view.h"
#include "pan/usenet-utils/filter-info.h"
#include "pan/usenet-utils/scorefile.h"

namespace {
log4cxx::LoggerPtr logger = pan::getLogger("article-filter");
}

using namespace pan;

std::string const ArticleFilter ::get_header(Article const &a,
                                            Quark const &header_name) const
{
  static std::string const empty;

  if (header_name == subject)
  {
    return a.get_subject().to_string();
  }
  if (header_name == from)
  {
    return a.get_author().to_string();
  }
  if (header_name == message_Id)
  {
    return a.message_id.to_string();
  }
  if (header_name == message_ID)
  {
    return a.message_id.to_string();
  }

  std::cerr << LINE_ID << ' ' << PACKAGE_STRING << " is misparsing \""
            << header_name << "\".\n"
            << "Please file a bug report to "
               "https://gitlab.gnome.org/GNOME/pan/issues\n";
  return empty;
}

// TODO: cannot test article by article. Need a way to test all in one query
// need to modify caller of test_article.
bool ArticleFilter ::test_article(Data const &data,
                                  FilterInfo const &criteria,
                                  Article const &article) const
{
  ArticleCache const &cache(data.get_cache());
  bool pass(false);
  switch (criteria._type)
  {
    case pan::FilterInfo::TYPE_TRUE:
      pass = true;
      break;

    case FilterInfo::AGGREGATE_AND:
      pass = true;
      foreach_const (FilterInfo::aggregatesp_t, criteria._aggregates, it)
      {
        // assume test passes if test needs body but article not cached
        if (! (*it)->_needs_body || cache.contains(article.message_id))
        {
          if (! test_article(data, **it, article))
          {
            pass = false;
            break;
          }
        }
      }
      break;

    case FilterInfo::AGGREGATE_OR:
      if (criteria._aggregates.empty())
      {
        pass = true;
      }
      else
      {
        pass = false;
        foreach_const (FilterInfo::aggregatesp_t, criteria._aggregates, it)
        {
          // assume test fails if test needs body but article not cached
          if (! (*it)->_needs_body || cache.contains(article.message_id))
          {
            if (test_article(data, **it, article))
            {
              pass = true;
              break;
            }
          }
        }
      }
      break;

    case FilterInfo::IS_BINARY:
      pass = article.get_part_state() == Article::COMPLETE;
      break;

    case FilterInfo::IS_POSTED_BY_ME:
      pass = data.has_from_header(article.get_author().to_view());
      break;

    case FilterInfo::IS_READ:
      pass = article.is_read();
      break;

    case FilterInfo::IS_UNREAD:
      pass = ! article.is_read();
      break;

    case FilterInfo::BYTE_COUNT_GE:
      pass = article.is_byte_count_ge((unsigned long)criteria._ge);
      break;

    case FilterInfo::CROSSPOST_COUNT_GE:
      pass = article.get_crosspost_count() >= criteria._ge;
      break;

    case FilterInfo::DAYS_OLD_GE:
      pass = (time(NULL) - article.get_time_posted()) > (criteria._ge * 86400);
      break;

    case FilterInfo::LINE_COUNT_GE:
      pass = article.is_line_count_ge((unsigned int)criteria._ge);
      break;

    case FilterInfo::TEXT:
      if (criteria._header == xref)
      {
        if (criteria._text._impl_type
            == TextMatch::CONTAINS) // user is filtering by groupname?
        {
          std::vector<StringView> cross_p_groups;
          StringView g;
          article.get_crosspost_groups(cross_p_groups);
          for (auto cross_p_groups : g)
          {
            if ((pass = criteria._text.test (g)))
              break;
          }
        }
        else if (criteria._text._impl_text.find("(.*:){")
                 != std::string::npos) // user is filtering by # of crossposts
        {
          char const *search = "(.*:){"; //}
          std::string::size_type pos =
            criteria._text._impl_text.find(search) + strlen(search);
          int const ge = atoi(criteria._text._impl_text.c_str() + pos);
          FilterInfo tmp;
          tmp.set_type_crosspost_count_ge(ge);
          pass = test_article(data, tmp, article);
        }
        else if (criteria._text._impl_text.find(".*:.*")
                 != std::string::npos) // user is filtering by # of crossposts?
        {
          StringView const v(criteria._text._impl_text);
          int const ge = std::count(v.begin(), v.end(), ':');
          FilterInfo tmp;
          tmp.set_type_crosspost_count_ge(ge);
          pass = test_article(data, tmp, article);
        }
        else // oh fine, then, user is doing some other damn thing with the xref
             // header.  build one for them.
        {
          pass = criteria._text.test (article.get_rebuilt_xref());
        }
      }
      else if (criteria._header == newsgroups)
      {
        pass = criteria._text.test (article.get_xrefed_groups());
      }
      else if (criteria._header == references)
      {
        std::string s;
        data.get_article_references(&article, s);
        pass = criteria._text.test(s);
      }
      else if (! criteria._needs_body)
      {
        pass = criteria._text.test(get_header(article, criteria._header));
      }
      else
      {
        if (cache.contains(article.message_id))
        {
          ArticleCache::mid_sequence_t mid(1, article.message_id);
#ifdef HAVE_GMIME_CRYPTO
          GPGDecErr err;
          GMimeMessage *msg = cache.get_message(mid, err);
#else
          GMimeMessage *msg = cache.get_message(mid);
#endif
          const char *hdr =
            g_mime_object_get_header(GMIME_OBJECT(msg), criteria._header);
          pass = criteria._text.test(hdr);
          g_object_unref(msg);
        }
        else
        {
          pass = false;
        }
      }
      break;

    case FilterInfo::SCORE_GE:
      pass = article.get_score() >= criteria._ge;
      break;

    case FilterInfo::IS_CACHED:
      pass = data.get_cache().contains(article.message_id);
      break;

    case FilterInfo::TYPE_ERR:
      assert(0 && "invalid type!");
      pass = false;
      break;
  }

  if (criteria._negate)
  {
    pass = ! pass;
  }

  return pass;
}

void ArticleFilter ::get_article_scores(Data const &data,
                                        sections_t const &sections,
                                        Quark const &group,
                                        Article const &article,
                                        Scorefile::items_t &setme) const
{
  for (Scorefile::Section section : sections)
  {
    foreach_const (Scorefile::items_t, section.items, it)
    {
      if (it->expired)
      {
        continue;
      }
      if (! test_article(data, it->test, article))
      {
        continue;
      }
      setme.push_back(*it);
      if (it->value_assign_flag)
      {
        return;
      }
    }
  }
}
