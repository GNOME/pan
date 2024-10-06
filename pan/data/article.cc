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
#include <SQLiteCpp/Statement.h>
#include <algorithm>
#include <bits/types/time_t.h>
#include <cassert>
#include <config.h>
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>

using namespace pan;

Article ::PartState Article ::get_part_state() const
{
  PartState part_state(SINGLE);

  // not a multipart
  if (! is_binary)
  {
    part_state = SINGLE;
  }

  // someone's posted a followup to a multipart
  else if (! is_line_count_ge(250) && has_reply_leader(subject.to_view()))
  {
    part_state = SINGLE;
  }

  else
  {
    const Parts::number_t total = parts.get_total_part_count();
    const Parts::number_t found = parts.get_found_part_count();
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
  return parts.get_total_part_count();
}

Parts::number_t Article::get_found_part_count() const
{
  return parts.get_found_part_count();
}

bool Article::get_part_info(Parts::number_t num,
                            std::string &mid,
                            Parts::bytes_t &bytes) const
{
  return parts.get_part_info(num, mid, bytes, message_id);
}

unsigned long Article::get_line_count() const
{
  return lines;
}

bool Article::is_line_count_ge(size_t test) const
{
  return lines >= test;
}

unsigned int Article ::get_crosspost_count() const
{
  quarks_t groups;
  foreach_const (Xref, xref, xit)
  {
    groups.insert(xit->group);
  }
  return (int)groups.size();
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
  mid_sequence_t mids;
  for (part_iterator it(pbegin()), end(pend()); it != end; ++it)
  {
    mids.push_back(it.mid());
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
    select name || ' <' || address || '>' from author as t
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
  assert(count > 0);
  return result;
}

void Article::set_author(Quark a) const {
  StringView name, address, auth(a);
  auth.pop_token(name, '<');
  name.trim();
  auth.pop_token(address, '>');
  SQLite::Statement author_q(pan_db, R"SQL(
    insert into author (name, address) values (?,?) on conflict do nothing
  )SQL");
  author_q.bind(1,name);
  author_q.bind(1,address);
  author_q.exec();

  SQLite::Statement q(pan_db, R"SQL(
    update article
    set author_id = (select id from author where adress = ?)
    where message_id = ?
  )SQL");
  q.bind(1,address);
  q.bind(2,message_id);
  assert(q.exec() == 1);
}

// const Quark&
// Article :: get_attachment () const
//{
//   for (part_iterator it(pbegin()), end(pend()); it!=end; ++it)
//     if (it.mid() == search)
// }

void Article ::clear()
{
  message_id.clear();
  // author.clear();
  subject.clear();
  // TODO: replace time_posted = 0;
  xref.clear();
  score = 0;
  parts.clear();
  is_binary = false;
}
