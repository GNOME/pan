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

#ifndef __FileQueue_h__
#define __FileQueue_h__

#include <pan/general/quark.h>
#include <pan/general/sorted-vector.h>
#include <pan/data/parts.h>
#include <pan/data/article.h>
#include <pan/data/xref.h>
#include <pan/data-impl/memchunk.h>
#include <string>
#include <list>
#include <map>

/****
*****
****/

namespace pan {

  class FileQueue
  {

     public:
      FileQueue();
      virtual ~FileQueue ();

        struct FileData
        {

          const char     * filename;
          const char     * basename;
          unsigned long    byte_count;
          unsigned long part_in_queue;
          FileData() {}
        };

        typedef std::list<FileData>::iterator articles_it;
        typedef std::list<FileData> articles_v;

     size_t size() { return _articles_v.size(); }
     articles_it end() { return _articles_v.end(); }
     articles_it begin() { return _articles_v.begin(); }
     bool empty() { return _articles_v.empty(); }


     public:
      enum InsertType
      { BEGIN, END };

    //own
    public:
      virtual void add (const char* filename,
                  FileQueue::InsertType type);

    private:
      articles_v _articles_v;
  };
}

#endif
