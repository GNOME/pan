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

#include "article.h"
#include "pan/data/pan-db.h"
#include "pan/general/log4cxx.h"
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <bits/types/time_t.h>
#include <cassert>
#include <config.h>
#include <cstdint>
#include <glib.h>
#include <log4cxx/logger.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include <stdexcept>
#include <vector>

using namespace pan;

namespace  {
log4cxx::LoggerPtr logger(getLogger("article"));
}

Article ::PartState Article ::get_part_state() const
{
  PartState part_state(SINGLE);

  // not a multipart
  if (! is_binary())
  {
    part_state = SINGLE;
  }

  // someone's posted a followup to a multipart
  else if (! is_line_count_ge(250) && has_reply_leader(get_subject().to_view()))
  {
    part_state = SINGLE;
  }

  else
  {
    SQLite::Statement q(pan_db, R"SQL(
      select expected_parts,
           (select count() from article_part as p
            where p.article_id == a.id) as found_nb
      from article as a
      where a.message_id = ?
  )SQL");
  q.bindNoCopy(1, message_id.c_str());

  int total(0), found(0);
  while (q.executeStep()) {
    total = q.getColumn(0).getInt64();
    found = q.getColumn(0).getInt64();
  }

  if (! found) // someone's posted a "000/124" info message
    {
      part_state = SINGLE;
    }
  else // a multipart..
    {
      part_state = total == found ? COMPLETE : INCOMPLETE;
    }
  }

  return part_state;
}

Parts::number_t Article::get_total_part_count () const
{
  SQLite::Statement q(pan_db, R"SQL(
    select expected_parts from article
    where article.message_id = ?
  )SQL");
  q.bindNoCopy(1, message_id.c_str());

  int count(0);
  while (q.executeStep()) {
    count = q.getColumn(0).getInt64();
  }
  return count;
}

Parts::number_t Article::get_found_part_count() const
{
  SQLite::Statement q(pan_db, R"SQL(
    select count() from article_part
    where article_id == (select id from article where message_id = ?)
  )SQL");
  q.bind(1,message_id);
  int64_t result(0);
  while (q.executeStep()) {
    result = q.getColumn(0).getInt();
  }
  return result;
}

unsigned long Article::get_line_count() const
{
  SQLite::Statement q(pan_db, "select line_count from article where message_id = ?");
  q.bind(1,message_id);
  int64_t result(0);
  int count(0);
  while (q.executeStep()) {
    result = q.getColumn(0).getInt();
    count++;
  }
  assert(count > 0);
  return result;
}

bool Article::is_line_count_ge(size_t test) const
{
  return get_line_count() >= test;
}

unsigned int Article ::get_crosspost_count() const
{
  SQLite::Statement q(pan_db, R"SQL(
    select count() from article_group as ag
    join article as a on a.id == ag.article_id
    where a.message_id == ?
  )SQL");

  q.bind(1,message_id);
  int count(0);
  while (q.executeStep()) {
    count = q.getColumn(0).getInt();
  }
  return count;
}

void Article ::get_crosspost_groups(std::vector<StringView> &setme) const
{
  SQLite::Statement q(pan_db, R"SQL(
    select g.name from `group` as g
    join article_group as ag on ag.group_id == g.id
    join article as a on a.id == ag.article_id
    where a.message_id == ?
    order by g.name
  )SQL");

  q.bindNoCopy(1,message_id);
  int count(0);
  while (q.executeStep()) {
    setme.push_back(StringView(q.getColumn(0).getText()));
  }
}

std::string Article ::get_rebuilt_xref() const
{
  SQLite::Statement q(pan_db, R"SQL(
    select s.host || " " || group_concat(g.name || ":" || x.number, " ") as xref
    from `group` as g
    join article_xref as xr on article_group_id = ag.id
    join server as s on server_id == s.id
    join article_group as ag on ag.group_id == g.id
    join article as a on a.id == ag.article_id
    where a.message_id == ?
    group by s.host
    order by g.name asc
  )SQL");

  q.bindNoCopy(1,message_id);
  std::string result;
  while (q.executeStep()) {
    result = q.getColumn(0).getText();
  }
  return result;
}

std::string Article ::get_xrefed_groups() const
{
  SQLite::Statement q(pan_db, R"SQL(
    select group_concat(g.name, ",") as xref from `group` as g
    join article_group as ag on ag.group_id == g.id
    join article as a on a.id == ag.article_id
    where a.message_id == ?
    group by g.name
    order by g.name asc
  )SQL");

  q.bindNoCopy(1,message_id);
  std::string result;
  while (q.executeStep()) {
    result = q.getColumn(0).getText();
  }
  return result;
}

bool Article ::has_reply_leader(StringView const &s)
{
  return ! s.empty() && s.len > 4 && (s.str[0] == 'R' || s.str[0] == 'r')
         && (s.str[1] == 'E' || s.str[1] == 'e') && s.str[2] == ':'
         && s.str[3] == ' ';
}

unsigned long Article ::get_byte_count() const
{
  unsigned long bytes = 0;
  for (part_iterator it(pbegin()), end(pend()); it != end; ++it)
  {
    bytes += it.bytes();
  }
  return bytes;
}

bool Article ::is_byte_count_ge(unsigned long test) const
{
  unsigned long bytes = 0;
  for (part_iterator it(pbegin()), end(pend()); it != end; ++it)
  {
    if (((bytes += it.bytes())) >= test)
    {
      return true;
    }
  }
  return false;
}

Article ::mid_sequence_t Article ::get_part_mids() const
{
  SQLite::Statement q(pan_db, R"SQL(
    select part_message_id from article_part as p
    join article as a on p.article_id == a.id
    where a.message_id = ?
    order by cast(part_number as integer)
  )SQL");
  q.bind(1,message_id);

  mid_sequence_t mids;
  while (q.executeStep()) {
    mids.push_back(Quark(q.getColumn(0).getText()));
  }
  return mids;
}

time_t Article::get_time_posted() const {
  SQLite::Statement q(pan_db, "select time_posted from article where message_id = ?");
  q.bind(1,message_id);
  time_t result = 0;
  int count(0);
  while (q.executeStep()) {
    // time may be 0 for some article
    result = q.getColumn(0).getInt();
    count++;
  }
  assert(count > 0);
  return result;
}

void Article::set_time_posted(time_t t) const {
  SQLite::Statement q(pan_db, "update article set time_posted = ? where message_id = ?");
  q.bind(1,t);
  q.bind(2,message_id);
  assert( q.exec() == 1);
}

Quark Article::get_author() const {
  SQLite::Statement q(pan_db, R"SQL(
    select author from author as t
    join article as a on a.author_id == t.id
      where a.message_id = ?
  )SQL");
  q.bind(1,message_id);
  Quark result;
  int count = 0;
  while (q.executeStep()) {
    result = Quark(q.getColumn(0).getText());
    count ++;
  }
  if (count == 0) {
    LOG4CXX_DEBUG(logger, "Could not find author of article " << message_id.c_str());
  }
  return result;
}

void Article::set_author(Quark a) const {
  SQLite::Statement author_q(pan_db, R"SQL(
    insert into author (author) values (?,?) on conflict do nothing
  )SQL");
  author_q.bind(1,a);
  author_q.exec();

  SQLite::Statement q(pan_db, R"SQL(
    update article
    set author_id = (select id from author where author = ?)
    where message_id = ?
  )SQL");
  q.bind(1,a);
  q.bind(2,message_id);
  assert(q.exec() == 1);
}

Quark Article::get_subject() const {
  SQLite::Statement q(pan_db, R"SQL(
    select subject from article where message_id = ?
  )SQL");
  q.bindNoCopy(1,message_id.c_str());
  Quark result;
  int count = 0;
  while (q.executeStep()) {
    result = Quark(q.getColumn(0).getText());
    count ++;
  }
  assert(count > 0);
  return result;
}

void Article::set_subject(Quark a) const {
  SQLite::Statement q(pan_db, R"SQL(
    update article set subject = ? where message_id = ?
  )SQL");
  q.bind(1,a);
  q.bind(2,message_id);
  assert( q.exec() == 1);
}

int Article::get_score() const {
  SQLite::Statement q(pan_db, R"SQL(
    select score from article where message_id = ?
  )SQL");
  q.bind(1,message_id);
  int result;
  int count = 0;
  while (q.executeStep()) {
    result = q.getColumn(0).getInt();
    count ++;
  }
  assert(count > 0);
  return result;
}

void Article::set_score(int s) const {
  SQLite::Statement q(pan_db, R"SQL(
    update article set score = ? where message_id = ?
  )SQL");
  q.bind(1,s);
  q.bind(2,message_id);
  assert( q.exec() == 1);
}

bool Article::is_binary() const {
  SQLite::Statement q(pan_db, R"SQL(
    select binary from article where message_id = ?
  )SQL");
  q.bind(1,message_id);
  bool result;
  int count = 0;
  while (q.executeStep()) {
    result = q.getColumn(0).getInt();
    count ++;
  }
  assert(count > 0);
  return result;
}

void Article::is_binary(bool b) const {
  SQLite::Statement q(pan_db, R"SQL(
    update article set binary = ? where message_id = ?
  )SQL");
  q.bind(1,b);
  q.bind(2,message_id);
  assert( q.exec() == 1);
}

void Article ::clear()
{
  message_id.clear();
  // author.clear();
  // subject.clear();
  // TODO: replace time_posted = 0;
  // score = 0;
  parts.clear();
  // is_binary = false;
}

/* Functions to bookmark an article */
void Article ::toggle_flag()
{
  LOG4CXX_TRACE(logger, "Toggle flag of " << message_id.c_str());
  SQLite::Statement q(pan_db, R"SQL(
    update article set flag = 1-flag where message_id = ?
  )SQL");
  q.bind(1,message_id);
  assert( q.exec() == 1);
}

bool Article ::get_flag() const
{
  LOG4CXX_TRACE(logger, "Get flag of " << message_id.c_str());
  SQLite::Statement q(pan_db, R"SQL(
    select flag from article where message_id = ?
  )SQL");
  q.bindNoCopy(1,message_id);
  while (q.executeStep()) {
    return q.getColumn(0).getInt() != 0 ;
  }
  throw std::invalid_argument("get_flag: unknown message_id");
}

void Article ::set_flag(bool setme)
{
  LOG4CXX_TRACE(logger, "Set flag of " << message_id.c_str() << " to " << setme);
  SQLite::Statement q(pan_db, R"SQL(
    update article set flag = ? where message_id = ?
  )SQL");
  q.bind(1,setme);
  q.bind(2,message_id);
  assert( q.exec() == 1);
}
