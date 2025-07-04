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

#include <SQLiteCpp/Transaction.h>
#include <config.h>
#include <cmath>
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
#include "data-impl.h"
#include "pan/general/log4cxx.h"
#include "pan/general/time-elapsed.h"

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
}

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

  on_articles_added (group, workarea._added_batch);
  workarea._added_batch.clear();
  on_articles_changed (group, workarea._changed_batch, true);
  workarea._changed_batch.clear();
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
  Quark art_mid;

  if (part_count > 1)
  {
    // walk through the articles we've already got for the group
    // to see if there's already an Article allocated to this
    // multipart.  If there is, we use it here instead of adding a new one

    // search the article in DB
    SQLite::Statement search_article_q(pan_db, R"SQL(
      select message_id
      from article as art
      join article_group as ag on ag.article_id == art.id
      join `group` as g on ag.group_id == g.id
      join author as auth on art.author_id == auth.id
      join subject on subject.id == art.subject_id
      where g.name == ?
        and subject.subject == ?
        and auth.author == ?
        and art.expected_parts == ?
      limit 1
    )SQL");

    // find all articles with same subject, author and part count to
    // make sure that found article if a good match for this
    // part. Note that part identifier like [3/50] are stripped before
    // being stored.
    search_article_q.reset();
    search_article_q.bind(1, group);
    search_article_q.bind(2, multipart_subject_quark);
    search_article_q.bind(3, author);
    search_article_q.bind(4, part_count);
    while (search_article_q.executeStep())
    {
        // ok, we'll use this article as the article for this part
      art_mid = Quark(search_article_q.getColumn(0).getText());
    }
  }


  if (art_mid.empty())
  {
    SQLite::Transaction add_article(pan_db);

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
      set_subject_q.bind(1, multipart_subject_quark);
      set_subject_q.exec();

      // Create the article in DB, line_count is updated in insert_part_in_db()
      SQLite::Statement create_article_q(pan_db, R"SQL(
        insert into `article` (author_id, subject_id, message_id, binary,
                               expected_parts, time_posted, `references`)
        values ((select id from author where author = ?),
                (select id from subject where subject = ?),?,?,?,?,?)
      )SQL");
      create_article_q.bind(1, author);
      create_article_q.bind(2, multipart_subject_quark);
      create_article_q.bind(3, art_mid);
      create_article_q.bind(4, part_count >= 1 );
      create_article_q.bind(5, part_count > 1 ? part_count : 1);
      create_article_q.bind(6, time_posted);
      if (!references.empty())
        create_article_q.bind(7, references);
      create_article_q.exec();

      insert_xref_in_db(server, art_mid, xref);
    }

    // now update the article thread from references. i.e. set
    // parent_id and ghost articles according to references values to
    // construct a tree of articles (missing articles are stored in
    // `ghost` table)
    store_references(message_id, references);

    add_article.commit();
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
  if ((time(nullptr) - workarea._last_flush_time) >= 10)
    xover_flush (group);

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
