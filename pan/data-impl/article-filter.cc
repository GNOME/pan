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
#include <pan/data/data.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>

// #include <glib/gprintf.h>
#include <glib.h>

#include "article-filter.h"

using namespace pan;

StringView const ArticleFilter ::get_header(Article const &a,
                                            Quark const &header_name) const
{
  static StringView const empty;

  if (header_name == subject)
  {
    return a.subject.to_view();
  }
  if (header_name == from)
  {
    return a.author.to_view();
  }
  if (header_name == message_Id)
  {
    return a.message_id.to_view();
  }
  if (header_name == message_ID)
  {
    return a.message_id.to_view();
  }

  std::cerr << LINE_ID << ' ' << PACKAGE_STRING << " is misparsing \""
            << header_name << "\".\n"
            << "Please file a bug report to "
               "https://gitlab.gnome.org/GNOME/pan/issues\n";
  return empty;
}

bool ArticleFilter ::test_article(Data const &data,
                                  FilterInfo const &criteria,
                                  Quark const &group,
                                  Article const &article) const
{
  ArticleCache const &cache(data.get_cache());
  bool pass(false);
  switch (criteria._type)
  {
    case FilterInfo::AGGREGATE_AND:
      pass = true;
      foreach_const (FilterInfo::aggregatesp_t, criteria._aggregates, it)
      {
        // assume test passes if test needs body but article not cached
        if (! (*it)->_needs_body || cache.contains(article.message_id))
        {
          if (! test_article(data, **it, group, article))
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
            if (test_article(data, **it, group, article))
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
      pass = data.has_from_header(article.author.to_view());
      break;

    case FilterInfo::IS_READ:
      pass = data.is_read(&article);
      break;

    case FilterInfo::IS_UNREAD:
      pass = ! data.is_read(&article);
      break;

    case FilterInfo::BYTE_COUNT_GE:
      pass = article.is_byte_count_ge((unsigned long)criteria._ge);
      break;

    case FilterInfo::CROSSPOST_COUNT_GE:
    {
      quarks_t groups;
      foreach_const (Xref, article.xref, xit)
      {
        groups.insert(xit->group);
      }
      pass = (int)groups.size() >= criteria._ge;
      break;
    }

    case FilterInfo::DAYS_OLD_GE:
      pass = (time(NULL) - article.time_posted) > (criteria._ge * 86400);
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
          foreach_const (Xref, article.xref, xit)
          {
            if ((pass = criteria._text.test(xit->group.to_view())))
            {
              break;
            }
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
          pass = test_article(data, tmp, group, article);
        }
        else if (criteria._text._impl_text.find(".*:.*")
                 != std::string::npos) // user is filtering by # of crossposts?
        {
          StringView const v(criteria._text._impl_text);
          int const ge = std::count(v.begin(), v.end(), ':');
          FilterInfo tmp;
          tmp.set_type_crosspost_count_ge(ge);
          pass = test_article(data, tmp, group, article);
        }
        else // oh fine, then, user is doing some other damn thing with the xref
             // header.  build one for them.
        {
          std::string s;
          foreach_const (Xref, article.xref, xit)
          {
            if (s.empty())
            {
              int unused;
              data.get_server_addr(xit->server, s, unused);
              s += ' ';
            }
            s += xit->group;
            s += ':';
            char buf[32];
            g_snprintf(buf,
                       sizeof(buf),
                       "%" G_GUINT64_FORMAT,
                       static_cast<uint64_t>(xit->number));
            s += buf;
            s += ' ';
          }
          if (! s.empty())
          {
            s.resize(s.size() - 1);
          }
          pass = criteria._text.test(s);
        }
      }
      else if (criteria._header == newsgroups)
      {
        quarks_t unique_groups;
        foreach_const (Xref, article.xref, xit)
        {
          unique_groups.insert(xit->group);
        }

        std::string s;
        foreach_const (quarks_t, unique_groups, git)
        {
          s += git->c_str();
          s += ',';
        }
        if (! s.empty())
        {
          s.resize(s.size() - 1);
        }
        pass = criteria._text.test(s);
      }
      else if (criteria._header == references)
      {
        std::string s;
        data.get_article_references(group, &article, s);
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
      pass = article.score >= criteria._ge;
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

void ArticleFilter ::test_articles(Data const &data,
                                   FilterInfo const &criteria,
                                   Quark const &group,
                                   articles_t const &in,
                                   articles_t &setme_pass,
                                   articles_t &setme_fail) const
{
  foreach_const (articles_t, in, it)
  {
    Article const *a(*it);
    if (test_article(data, criteria, group, *a))
    {
      setme_pass.push_back(a);
    }
    else
    {
      setme_fail.push_back(a);
    }
  }
}

int ArticleFilter ::score_article(Data const &data,
                                  sections_t const &sections,
                                  Quark const &group,
                                  Article const &article) const
{
  int score(0);
  foreach_const (sections_t, sections, sit)
  {
    Scorefile::Section const *section(*sit);
    foreach_const (Scorefile::items_t, section->items, it)
    {
      if (it->expired)
      {
        continue;
      }
      if (! test_article(data, it->test, group, article))
      {
        continue;
      }
      if (it->value_assign_flag)
      {
        return it->value;
      }
      score += it->value;
    }
  }
  return score;
}

void ArticleFilter ::get_article_scores(Data const &data,
                                        sections_t const &sections,
                                        Quark const &group,
                                        Article const &article,
                                        Scorefile::items_t &setme) const
{
  foreach_const (sections_t, sections, sit)
  {
    Scorefile::Section const *section(*sit);
    foreach_const (Scorefile::items_t, section->items, it)
    {
      if (it->expired)
      {
        continue;
      }
      if (! test_article(data, it->test, group, article))
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
