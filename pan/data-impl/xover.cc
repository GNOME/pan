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

#include <config.h>
#include <SQLiteCpp/Transaction.h>
#include <glib/gi18n.h>
#include <gmime/gmime.h>
#include <log4cxx/logger.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/data/article.h>
#include <string>
#include "data-impl.h"
#include "pan/general/log4cxx.h"
#include "pan/general/time-elapsed.h"
#include <fmt/format.h>

using namespace pan;

namespace {
log4cxx::LoggerPtr logger = pan::getLogger("xover");

bool parse_multipart_subject(StringView const &subj,
                             int &part,
                             int &parts,
                             std::string &no_part)
{
  char const *numerator = nullptr;
  char const *denominator = nullptr;

  char const *s(subj.begin());
  char const *pch(subj.end());
  while (pch != s)
  {
    // find the ']' of [n/N]
    --pch;
    if ((pch[1] != ')' && pch[1] != ']') || ! isdigit(*pch))
      continue;

    // find the '/' of [n/N]
    while (s != pch && isdigit(*pch))
      --pch;
    if (s == pch || (*pch != '/' && *pch != '|'))
      continue;

    // N -> parts
    denominator = pch + 1;
    --pch;

    // find the '[' of [n/N]
    while (s != pch && isdigit(*pch))
      --pch;
    if (s == pch || (*pch != '(' && *pch != '['))
    {
      denominator = nullptr;
      continue;
    }

    // N -> part
    numerator = pch + 1;
    char *numerator_end(nullptr);
    part = (int)strtol(numerator, &numerator_end, 10);
    parts = atoi(denominator);

    if (part > parts)
    {
      // false positive...
      numerator = denominator = nullptr;
      part = parts = 0;
      continue;
    }

    no_part.assign(subj.str, numerator - subj.str);
    no_part.append(numerator_end, (subj.str + subj.len) - numerator_end);
    return true;
  }

  return false;
}

  void find_parts(StringView const &subj,
                  Quark const &group,
                  int line_count,
                  int &part,
                  int &parts,
                  std::string &norm)
  {
    if (!parse_multipart_subject (subj, part, parts, norm))
    {
      part = parts = 0;
      norm.assign (subj.str, subj.len);
    }

    /* if not a multipart yet, AND if it's a big message, AND
       it's either in one of the pictures/fan/sex groups or it
       has commonly-used image names in the subject, it's probably
       a single-part binary */
    if (!parts && line_count>400) {
      const StringView gname (group.c_str());
      if (((gname.strstr("binaries") ||
            gname.strstr("fan") ||
            gname.strstr("mag") ||
            gname.strstr("sex")))
           ||
          ((subj.strstr(".jpg") || subj.strstr(".JPG") ||
            subj.strstr(".jpeg") || subj.strstr(".JPEG") ||
            subj.strstr(".gif") || subj.strstr(".GIF") ||
            subj.strstr(".png") || subj.strstr(".PNG"))))
        part = parts = 1;
    }

    /* but if it's starting the subject with "Re:" and doesn't have
       many lines, it's probably a followup to a part, rather than an
       actual part. Note that server may provide a bogus zero line
       count in which case the last test is misleading. */
    if (Article::has_reply_leader(subj) && line_count != 0 && line_count < 100)
      part = parts = 0;

    /* Subjects containing (0/N) aren't part of an N-part binary;
       they're text description articles that accompany the binary. */
    if (part == 0)
      parts = 0;
  }

  DataImpl::ArticleInfo
  find_article_in_db(Quark const &group, StringView const &author,
                     std::string const &multipart_subject, int part_count) {
    DataImpl::ArticleInfo result;
    TimeElapsed timer;

    // search the article in DB
    // first try subject alone
    SQLite::Statement search_subject_q(pan_db, R"SQL(
      select id from subject where subject.subject == ?
    )SQL");
    search_subject_q.bindNoCopy(1, multipart_subject);
    int subject_id(0);
    while (search_subject_q.executeStep()) {
      subject_id = search_subject_q.getColumn(0);
    }
    if (subject_id == 0) {
      LOG4CXX_DEBUG(logger, "did not find subject in " << timer.get_seconds_elapsed() << "s");
      // subject does not exist, no need to check elsewhere
      return {Quark(), Article::INCOMPLETE};
    }

    // find all articles with same subject, author and part count to
    // make sure that found article if a good match for this
    // part. Note that part identifier like [3/50] are stripped before
    // being stored.
    SQLite::Statement search_article_q(pan_db, R"SQL(
      select message_id, part_state, server.pan_id
      from article as art
      join article_group as ag on ag.article_id == art.id
      join `group` as g on ag.group_id == g.id
      join author as auth on art.author_id == auth.id
      join article_xref as xref on xref.article_group_id = ag.id
      join server on server.id == xref.server_id
      where g.name == ?
        and art.subject_id == ?
        and auth.author == ?
        and art.expected_parts == ?
      limit 1
    )SQL");

    // find all articles with same subject, author and part count to
    // make sure that found article if a good match for this
    // part. Note that part identifier like [3/50] are stripped before
    // being stored.
    search_article_q.reset();
    search_article_q.bindNoCopy(1, group);
    search_article_q.bind(2, subject_id);
    search_article_q.bind(3, author);
    search_article_q.bind(4, part_count);
    std::string part_state;

    // get one line per server for this article
    while (search_article_q.executeStep()) {
      // ok, we'll use this article as the article for this part
      Quark mid(search_article_q.getColumn(0).getText()),
        pan_id(search_article_q.getColumn(2).getText());
      if (result.mid.empty()) {
        // fill data on first pass
        Article a(group, mid);
        result.mid = mid;
        part_state = search_article_q.getColumn(1).getText();
        result.part_state = a.char_to_state(part_state.c_str()[0]);
      }
      result.server_ids.insert(pan_id);
    }

    if (result.mid.empty())
      LOG4CXX_DEBUG(logger, "did not find article in "
                                << timer.get_seconds_elapsed() << "s");
    else
      LOG4CXX_DEBUG(logger, "found article " << result.mid << " status "
                                             << part_state << " in "
                                             << timer.get_seconds_elapsed()
                                             << "s");
    return result;
  }

  void add_article_in_db(Quark const &group, Quark const &art_mid,
                         StringView const &author,
                         std::string const &multipart_subject, int part_count,
                         int time_posted, std::string const &references) {
    // Create author
    SQLite::Statement set_author_q(pan_db, R"SQL(
        insert into `author` (author) values (?) on conflict do nothing
      )SQL");
    set_author_q.bind(1, author);
    set_author_q.exec();

    // create subject
    SQLite::Statement set_subject_q(pan_db, R"SQL(
        insert into `subject` (subject) values (?) on conflict do nothing
      )SQL");
    set_subject_q.bind(1, multipart_subject);
    set_subject_q.exec();

    // Create the article in DB, line_count is updated in insert_part_in_db()
    SQLite::Statement create_article_q(pan_db, R"SQL(
        insert into `article` (author_id, subject_id, message_id, binary,
                               expected_parts, part_state, time_posted)
        values ((select id from author where author = ?),
                (select id from subject where subject = ?),?,?,?,?,?)
      )SQL");
    create_article_q.bind(1, author);
    create_article_q.bind(2, multipart_subject);
    create_article_q.bind(3, art_mid);
    create_article_q.bind(4, part_count >= 1);
    create_article_q.bind(5, part_count > 1 ? part_count : 1);
    create_article_q.bind(6, part_count > 1 ? "I" : "S");
    create_article_q.bind(7, time_posted);
    create_article_q.exec();

    if (!references.empty()) {
      SQLite::Statement set_ref_header_q(pan_db, R"SQL(
          insert into ref_header (article_id, ref_header)
             values ((select id from article where message_id = ?),?)
        )SQL");
      set_ref_header_q.bind(1, art_mid);
      set_ref_header_q.bind(2, references);
    }
  }
} // namespace

void DataImpl ::xover_clear_workarea(Quark const &group)
{
   LOG4CXX_DEBUG(logger, "Clearing the XOVER workearea for " << group);

   _xovers.erase (group);
   if (group == _cached_xover_group) {
      _cached_xover_group.clear ();
      _cached_xover_entry = nullptr;
   }
}

DataImpl ::XOverEntry &DataImpl ::xover_get_workarea(Quark const &group)
{
   XOverEntry * entry (nullptr);
   if (group == _cached_xover_group)
      entry = _cached_xover_entry;
   else {
      _cached_xover_group = group;
      _cached_xover_entry = entry = &_xovers[group];
   }
   return *entry;
}

void DataImpl ::xover_ref(Quark const &group)
{
  // sanity clause
  pan_return_if_fail (!group.empty());

  // ref the articles
  ref_group (group);

  // ref the xover
  XOverEntry& workarea (xover_get_workarea (group));
  ++workarea.refcount;
}

void DataImpl ::xover_flush(Quark const &group)
{
  XOverEntry& workarea (xover_get_workarea (group));

  // commit current changes
  if (workarea._add_article_transaction != nullptr) {
    LOG4CXX_DEBUG(logger, "Committing add article DB transaction");
    workarea._add_article_transaction->commit();
    free(workarea._add_article_transaction);
    workarea._add_article_transaction = nullptr;
  }

  on_articles_added (group, workarea._added_batch);
  workarea._added_batch.clear();

  // not a new article, but articles with new parts.
  update_part_states(workarea._changed_batch);
  on_articles_changed (group, workarea._changed_batch, false);
  workarea._changed_batch.clear();

  update_article_tables_and_gui();

  workarea._last_flush_time = time(nullptr);
}

void DataImpl ::xover_unref(Quark const &group)
{
  XOverEntry& workarea (xover_get_workarea (group));
  if (!--workarea.refcount)
  {
    xover_flush (group);
    xover_clear_workarea (group);
  }

  unref_group (group);
}

Article const *DataImpl ::xover_add(Quark const &server,
                                    Quark const &group,
                                    StringView const &subject,
                                    StringView const &author,
                                    const time_t time_posted,
                                    StringView const &message_id,
                                    StringView const &references_in,
                                    unsigned long const byte_count,
                                    unsigned long const line_count,
                                    StringView const &xref,
                                    bool const is_virtual)
{
  TimeElapsed timer;

  if (is_virtual)
    ref_group(group);

  GroupHeaders * h (get_group_headers (group));
  if (!h && !is_virtual) {
    Log::add_err_va (_("Error reading from %s: unknown group \"%s\""),
                     get_server_address(server).c_str(),
                     group.c_str());
    return nullptr;
  }


//  std::cerr<<"xover add : "<<subject<<" "<<author<<" "<<message_id<<" lines "<<line_count<<" bytes "<<byte_count<<std::endl;

  Article const *new_article(nullptr);

  XOverEntry& workarea (xover_get_workarea (group));
  const std::string references (
    GNKSA :: remove_broken_message_ids_from_references (references_in));

  /***
  **** Multipart Handling
  ***/
  /** multipart detection.  Pan folds multipart posts into
      a single Article holding all the parts.  This code decides,
      when we get a new multipart post, which Article to fold
      it into.  We strip out the unique part info from the Subject header
      (such as the "15" in [15/42]) and use it as a key in subject
      table that gives the Message-ID of the Article owning this post. */

  h->_dirty = true;

  int part_index, part_count;
  std::string multipart_subject;
  find_parts (subject, group, line_count, part_index, part_count, multipart_subject);
  const Quark multipart_subject_quark (multipart_subject);
  ArticleInfo art_info;
  std::string cache_key = group.to_string() + author.to_string() +
                          multipart_subject + std::to_string(part_count);

  if (part_count > 1) {
    auto search = _mid_cache.find(cache_key);
    if (search != _mid_cache.end()) {
      art_info.mid = search->second.mid;
      LOG4CXX_TRACE(logger, "found cached mid "
                                << art_info.mid << " for key " << cache_key << " in "
                                << timer.get_seconds_elapsed() << " s.");
    } else {
      // walk through the articles we've already got for the group
      // to see if there's already an Article allocated to this
      // multipart.  If there is, we use it here instead of adding a new one
      art_info = find_article_in_db(group, author, multipart_subject, part_count);
      // memoize the result
      if (!art_info.mid.empty()) {
        _mid_cache[cache_key] = art_info;
        LOG4CXX_TRACE(logger,
                      fmt::format("added mid {} in cache for key {} in {} s.",
                                  art_info.mid == nullptr
                                      ? "<null>"
                                      : art_info.mid.c_str(),
                                  cache_key,
                                  timer.get_seconds_elapsed()
                                  )
            );
      }
    }

    // drop line if article is known and complete
    if (!art_info.mid.empty() && art_info.part_state == Article::COMPLETE) {
      auto ids = art_info.server_ids;
      auto search = ids.find(server);
      // store the xref if the article comes from a new server
      if (search == ids.end())
        insert_xref_in_db(server, art_info.mid, xref);
      return nullptr;
    }
  }

  Quark art_mid(art_info.mid);

  if (workarea._add_article_transaction == nullptr) {
    LOG4CXX_TRACE(logger, "Creating add article DB transaction");
    workarea._add_article_transaction = new SQLite::Transaction (pan_db);
  }

  if (art_mid.empty())
  {
    art_mid = message_id;

    SQLite::Statement search_article_q(pan_db, R"SQL(
        select count() from `article` where message_id = ?
    )SQL");

    search_article_q.bind(1, art_mid);
    int count(0);
    while (search_article_q.executeStep()) {
      count = search_article_q.getColumn(0);
    }
    // if we don't have this article in DB
    if (count == 0) {
      workarea._added_batch.insert (art_mid);

      add_article_in_db(group, art_mid, author, multipart_subject, part_count,
                        time_posted, references);

      insert_xref_in_db(server, art_mid, xref);

      // memoize the result
      auto part_state = part_count > 1 ? Article::INCOMPLETE
        : Article::SINGLE;
      _mid_cache[cache_key] = ArticleInfo({art_mid, part_state});
    }

    // now update the article thread from references. i.e. set
    // parent_id and ghost articles according to references values to
    // construct a tree of articles (missing articles are stored in
    // `ghost` table)
    process_references(message_id, references);

    // TODO: handle this by batch, trigger on article with non null
    // references and null ghost or parent_id

    LOG4CXX_DEBUG(logger, "Added " << part_count << " part article " << art_mid
                                   << " to DB in "
                                   << timer.get_seconds_elapsed() << "s");
  }

  /**
  ***  Add the article's part info
  **/

  {
    int const number(part_count < 2 ? 1 : part_index);
    insert_part_in_db (group, art_mid,
               number, message_id,
               line_count, byte_count);
  }

  if (!workarea._added_batch.count(art_mid))
    workarea._changed_batch.insert(art_mid);

  double const duration = timer.get_seconds_elapsed();
  LOG4CXX_TRACE(logger, "Saved part "
               << message_id << " of article " << art_mid << "in " << duration << "s.");

  // maybe flush the batched changes
  // TODO: check if the change applied during flush won't clobber what was setup
  if ((time(nullptr) - workarea._last_flush_time) >= 10) {
    xover_flush(group);
    // clear the cache as applying rules during flush may delete
    // articles from DB, which would invalidate the cache entries of
    // the deleted articles.
    _mid_cache.clear();
  }

  if (is_virtual)
      unref_group(group);

  return new_article;
}

void DataImpl ::insert_xref_in_db(Quark const &server,
                                   Quark const msg_id,
                                   StringView const &line) {
  pan_return_if_fail(! server.empty());

  LOG4CXX_TRACE(logger, "insert xref on: " << line);

  // trim & cleanup; remove leading "Xref: " if present
  StringView xref(line);
  xref.trim();
  if (xref.len > 6 && ! memcmp(xref.str, "Xref: ", 6))
  {
    xref = xref.substr(xref.str + 6, NULL);
    xref.trim();
  }

  SQLite::Statement set_article_group_q(pan_db,R"SQL(
    insert into `article_group` (article_id, group_id)
    values (
      (select id from article where message_id = ?),
      (select id from `group` where name = ?)
    ) on conflict (article_id, group_id) do nothing;
  )SQL");

  SQLite::Statement set_xref_q(pan_db,R"SQL(
    insert into `article_xref` (article_group_id, server_id, number)
    values (
      (select ag.id from article_group as ag
       join `group` as g on g.id == ag.group_id
       join article as a on a.id == ag.article_id
       where a.message_id = ? and g.name = ?),
      (select id from server where pan_id = ?),
      ?
    ) on conflict (article_group_id, server_id) do nothing;
  )SQL");

  // walk through the xrefs, of format "group1:number group2:number"
  StringView s;
  while (xref.pop_token(s))
  {
    if (s.strchr(':') != nullptr)
    {
      StringView group_name;
      if (s.pop_token(group_name, ':'))
      {
        // insert group name as pseudo group if it's unknown
        add_group_in_db(server, group_name, true);

        set_article_group_q.reset();
        set_article_group_q.bind(1,msg_id);
        set_article_group_q.bind(2,group_name);
        set_article_group_q.exec();

        // update xref table
        set_xref_q.reset();
        set_xref_q.bind(1, msg_id);
        set_xref_q.bind(2, group_name);
        set_xref_q.bind(3, server);
        set_xref_q.bind(4, s);
        int count = set_xref_q.exec();
        LOG4CXX_TRACE(logger, "inserted " << count << " xref with msg id " << msg_id.c_str() <<
                      " group " << group_name << " server " << server.c_str() << " number " << s);
      }
    }
  }
}
