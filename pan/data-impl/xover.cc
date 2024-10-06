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
#include <cstdint>
#include <fstream>
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

using namespace pan;

namespace
{
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

    /* but if it's starting the subject with "Re:" and doesn't
       have many lines, it's probably a followup to a part, rather
       than an actual part. */
    if (Article::has_reply_leader(subj) && line_count<100)
      part = parts = 0;

    /* Subjects containing (0/N) aren't part of an N-part binary;
       they're text description articles that accompany the binary. */
    if (part == 0)
      parts = 0;
  }
}

void DataImpl ::xover_clear_workarea(Quark const &group)
{
   pan_debug ("Clearing the XOVER workearea for " << group);

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

  // populate the normalized lookup for multipart detection...
  GroupHeaders * h (get_group_headers (group));
  foreach_const (nodes_t, h->_nodes, it) {
      Quark const &mid(it->first);
      Article const *a(it->second->_article);
      if (a != nullptr)
        workarea._subject_lookup.insert(
          std::pair<Quark, Quark>(a->get_subject(), mid));
  }
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

void DataImpl::set_reference_tree_in_db(time_t const &time_posted,
                         StringView const &message_id,
                         std::string const &references)
{
  StringView ref_view(references), parent_msg_id;
  int parent_id(0);
  std::string current_msg_id(message_id);

  LOG4CXX_TRACE(_db_logger,
                  "message " << message_id << ": build ref tree from " << references);
  while (ref_view.pop_last_token(parent_msg_id))
  {
    LOG4CXX_TRACE(_db_logger,
                  "message " << message_id << ": scanning reference " << parent_msg_id);
    // avoid self loop. Some articles wrongly reference themselves
    if (parent_msg_id == message_id)
    {
        LOG4CXX_WARN(_db_logger,
                     "message " << message_id << " references itself");
        continue;
    }

    SQLite::Statement get_author_id(
      pan_db, "select author_id from article where message_id = ?");

    int p_count(0), author_id(0);

    // try to find parent id using parent_msg_id
    get_author_id.bind(1, parent_msg_id);
    while (get_author_id.executeStep()) {
      p_count ++;
      author_id = get_author_id.getColumn(0);
    }

    if (p_count == 0)
    {
      LOG4CXX_TRACE(_db_logger,
                    "message " << message_id << ": creating dummy parent article for " << parent_msg_id );
        // parent not found, create dummy article
        SQLite::Statement set_dummy_article_q(pan_db, R"SQL(
          insert into `article` (message_id, time_posted) values (?,?)
        )SQL");
        set_dummy_article_q.bind(1, parent_msg_id);
        // re-use time stamp for expiration
        set_dummy_article_q.bind(2, static_cast<int64_t>(time_posted));
        set_dummy_article_q.exec();
    } else {
      LOG4CXX_TRACE(_db_logger,
                    "message " << message_id << ": update existing parent article for " << parent_msg_id );
    }

    // now, update parent_id
    SQLite::Statement set_parent_id(pan_db, R"SQL(
        update `article` set parent_id = (select id from `article` where message_id = ?) where message_id = ?
      )SQL");
    set_parent_id.reset();
    set_parent_id.bind(1, parent_msg_id);
    set_parent_id.bind(2, current_msg_id);
    set_parent_id.exec();

    if (author_id != 0) {
      LOG4CXX_TRACE(_db_logger,
                    "message " << message_id << ": parent " << parent_msg_id << "is real article -> break loop");
      // parent is a real article. Its parent_id and the chain
      // above are assumed to be correct.
      break;
    }

    // for next loop, if any
    current_msg_id = parent_msg_id;
  }
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
      select count() from article as art join author as auth
        where art.message_id == ? and art.author_id == auth.id and art.author == ?
              and art.expected_parts == ?
    )SQL");

    typedef XOverEntry::subject_to_mid_t::const_iterator cit;
    const std::pair<cit,cit> range (workarea._subject_lookup.equal_range (multipart_subject_quark));
    for (cit it(range.first), end(range.second); it!=end && art_mid.empty(); ++it) {
        Quark const &candidate_mid(it->second);
        Article const *candidate(h->find_article(candidate_mid));
        // TODO: replace with a search in DB
        if (candidate && (candidate->get_author() == author)
            && ((int)candidate->get_total_part_count() == part_count))
          art_mid = candidate_mid;

        // check operation using DB
        search_article_q.reset();
        search_article_q.bind(1, it->second); // aka candidate_mid
        search_article_q.bind(2, author);
        search_article_q.bind(3, part_count);
        while (search_article_q.executeStep()) {
          if (search_article_q.getColumn(0).getInt() == 1) {
            // article for this part was found in DB
            // check that result is consistent with above (temporary)
            assert(art_mid == candidate_mid );
            // later:  art_mid = candidate_mid;
          }
        }
    }
  }


  if (art_mid.empty())
  {
    SQLite::Transaction add_article(pan_db);

    art_mid = message_id;

    // TODO: find a similar fuzzy search using DB
    if (part_count > 1)
      workarea._subject_lookup.insert(std::pair<Quark,Quark>(multipart_subject_quark, art_mid));

    // if we don't already have this article in memory...
    if (!h->find_article (art_mid))
    {
      //std::cerr << LINE_ID << " We didn't have this article yet, so creating an instance..." << std::endl;
      // load article data in memory. Will be removed
      Article& a (h->alloc_new_article());
      a.message_id = art_mid;
      a.is_binary = part_count >= 1;
      a.set_part_count (a.is_binary ? part_count : 1);
      a.xref.insert (server, xref);
      // build article tree in memory. Will be removed
      load_article (group, &a, references);
      new_article = &a;

      workarea._added_batch.insert (art_mid);
    }

    SQLite::Statement search_article_q(pan_db, R"SQL(
        select author_id from `article` where message_id = ?
    )SQL");

    search_article_q.bind(1, art_mid);
    int count(0);
    bool is_zombie(false);
    while (search_article_q.executeStep()) {
      count++;
      // we may have a zombie article found only in references field
      // of another article in the thread
      is_zombie = search_article_q.getColumn(0).getInt() == 0;
    }

    // if we don't have this article in DB
    if (count == 0 or is_zombie) {
      // Create author
      SQLite::Statement set_author_q(pan_db, R"SQL(
        insert into `author` (author) values (?) on conflict do nothing
      )SQL");
      set_author_q.bind(1,author);
      set_author_q.exec();
    }

    if (count == 0) {
      // Create the article in DB
      SQLite::Statement create_article_q(pan_db, R"SQL(
        insert into `article` (author_id,subject,message_id, binary, expected_parts,
                               time_posted, `references`, line_count)
        values ((select id from author where author = ?),?,?,?,?,?,?,?)
      )SQL");
      create_article_q.bind(1, author);
      create_article_q.bind(2, multipart_subject_quark);
      create_article_q.bind(3, art_mid);
      create_article_q.bind(4, part_count >= 1 );
      create_article_q.bind(5, part_count > 1 ? part_count : 1);
      create_article_q.bind(6, time_posted);
      create_article_q.bind(7, references);
      create_article_q.bind(8, static_cast<int64_t>(line_count));
      create_article_q.exec();

      insert_xref_in_db(server, art_mid, xref);
    } else if (is_zombie) {
      // update article data in DB
      SQLite::Statement update_article_q(pan_db, R"SQL(
        update `article` set (author_id,subject, binary, expected_parts,
                               time_posted, `references`, line_count)
        = ((select id from author where author = ?),?,?,?,?,?,?)
        where message_id == ?
      )SQL");
      update_article_q.bind(1, author);
      update_article_q.bind(2, multipart_subject_quark);
      update_article_q.bind(3, part_count >= 1 );
      update_article_q.bind(4, part_count > 1 ? part_count : 1);
      update_article_q.bind(5, time_posted);
      update_article_q.bind(6, references);
      update_article_q.bind(7, static_cast<int64_t>(line_count));
      update_article_q.exec();

      insert_xref_in_db(server, art_mid, xref);
    }

    // now update the article thread from references. i.e. set
    // parent_id extracted from references to construct a tree of
    // articles
    set_reference_tree_in_db(time_posted, message_id, references);

    add_article.commit();
  }

  /**
  ***  Add the article's part info
  **/

  {
    int const number(part_count < 2 ? 1 : part_index);
    load_part (group, art_mid,
               number, message_id,
               line_count, byte_count);
    insert_part_in_db (group, art_mid,
               number, message_id,
               line_count, byte_count);
  }

  if (!workarea._added_batch.count(art_mid))
    workarea._changed_batch.insert(art_mid);

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

  LOG4CXX_TRACE(_db_logger, "insert xref on: " << line);

  // trim & cleanup; remove leading "Xref: " if present
  StringView xref(line);
  xref.trim();
  if (xref.len > 6 && ! memcmp(xref.str, "Xref: ", 6))
  {
    xref = xref.substr(xref.str + 6, NULL);
    xref.trim();
  }

  SQLite::Statement set_xref_q(pan_db, R"SQL(
    insert into `article_xref` (article_id, group_id, server_id, number)
    values (
      (select id from article where message_id = ?),
      (select id from `group` where name = ?),
      (select id from server where pan_id = ?),
      ?
    ) on conflict (article_id, group_id, server_id) do nothing;
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

        // update xref table
        set_xref_q.reset();
        set_xref_q.bind(1, msg_id.c_str());
        set_xref_q.bind(2, group_name);
        set_xref_q.bind(3, server.c_str());
        set_xref_q.bind(4, s);
        int count = set_xref_q.exec();
        LOG4CXX_TRACE(_db_logger, "inserted " << count << " xref with msg id " << msg_id.c_str() <<
                      " group " << group_name << " server " << server.c_str() << " number " << s);
      }
    }
  }
}
