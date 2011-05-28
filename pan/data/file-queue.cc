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
#include <iostream>
#include <functional>

#include <config.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/data/xref.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/task.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file-queue.h"

using namespace pan;

FileQueue :: ~FileQueue ()
{}

FileQueue :: FileQueue() {}

void
FileQueue :: add (const char* filename,
                  FileQueue::InsertType type)
{
  static FileData a;
  struct stat sb;
  a.filename = std::string(filename);
  a.basename = std::string(g_path_get_basename(filename));
  stat(filename,&sb);
  a.byte_count = sb.st_size;

  type == END ?
    _articles_v.push_back(a) :
    _articles_v.push_front(a);
}

void
FileQueue :: remove(const articles_v& no)
{
  articles_const_it it = no.begin();
  articles_it vit;
  for ( vit = _articles_v.begin() ; vit != _articles_v.end(); ++vit) {
    for ( ; it != no.end(); ++it) {
      if (vit->filename.compare(it->filename)==0) {
        vit = _articles_v.erase(vit);
      }
    }
  }
}

void
FileQueue :: move_up(const articles_v& no, int pos)
{
  if (size()==1) return;
  articles_it it;
  for (it = _articles_v.begin(); it != _articles_v.end(); ++it)
  {

  }
}

void
FileQueue :: move_down(const articles_v& no, int pos)
{
  if (size()==1) return;
    articles_it it;
  for (it = _articles_v.begin(); it != _articles_v.end(); ++it)
  {

  }
}

void
FileQueue :: move_top(const articles_v& no)
{
  if (size()==1) return;
//  remove(no);
//  articles_r_it rit;
//  for ( rit=no.rbegin() ; rit != no.rend(); ++rit )
//    _articles_v.push_front(*rit);
}

void
FileQueue :: move_bottom(const articles_v& no)
{
  if (size()==1) return;
//  remove(no);
//  articles_const_it it;
//  for ( it= no.begin() ; it != no.end(); ++it )
//    _articles_v.push_back(*it);
}



