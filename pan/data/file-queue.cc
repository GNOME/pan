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


#include <algorithm>

#include <config.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>
#include "file-queue.h"

using namespace pan;

FileQueue :: ~FileQueue ()
{}

FileQueue :: FileQueue() {}

Article::PartState
FileQueue :: get_part_state () const {
  return COMPLETE;
}

void
FileQueue :: add (const StringView    & subject,
                  const StringView    & author,
                  const StringView    & filename,
                  const unsigned long   byte_count,
                  const unsigned long   line_count,
                  FileQueue::InsertType type)
{
  static FileData a;
  a.subject = subject;
  a.author = author;
  a.filename = filename;
  a.byte_count = byte_count;
  a.line_count = line_count;
  a.article.is_binary = true;
  a.article.set_part_count (1); // decoder has to determine that later on
//  const FileData* new_article = &a;
//  type == FileQueue::END ?
    _articles_v.push_back(a);
//    _articles_v.push_front(a);
}

//void
//FileQueue :: delete_single(const FileData* s)
//{
//  std::set<const FileData*>::iterator it = _articles_v.find(s);
//  _articles_v.erase(it);
//}
//
//void
//FileQueue :: delete_range(const Article* a, const Article* b)
//{
//
//  articles_it a_it = _articles_v.find(a);
//  articles_it b_it = _articles_v.find(b);
//  if (a==b ) delete_single(a);
//  _articles_v.erase ( a_it, b_it );
//}



