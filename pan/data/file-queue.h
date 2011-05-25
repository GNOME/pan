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

  class FileQueue: public Article
  {

     public:
      FileQueue();
      virtual ~FileQueue ();

        struct FileData
        {
          StringView     subject;
          StringView     author;
          StringView     filename;
          unsigned long  byte_count;
          unsigned long  line_count;
          Article        article;
          FileData() {}
        };

        typedef std::vector<FileData>::iterator articles_it;
        typedef std::vector<FileData> articles_v;


     articles_it end() { return _articles_v.end(); }
     articles_it begin() { return _articles_v.begin(); }
     FileData get_front() { return _articles_v[0]; }
     FileData get_at(int i) { return _articles_v[(i<0 || i>_articles_v.size()) ? 0 : i]; }
     bool empty() { return _articles_v.empty(); }

     articles_v& v() { return _articles_v; }

     public:
      enum InsertType
      { BEGIN, END };
     // article
     public:
      PartState get_part_state () const;

    //own
    public:
      void add (const StringView    & subject,
                const StringView    & author,
                const StringView    & filename,
                const unsigned long   byte_count,
                const unsigned long   line_count,
                FileQueue::InsertType type);

    private:
      articles_v _articles_v;
  };
}

#endif

// todo
//void
//TaskPane :: get_selected_tasks_foreach (GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer list_g)
//{
//  Task * task (0);
//  gtk_tree_model_get (model, iter, COL_TASK_POINTER, &task, -1);
//  static_cast<tasks_t*>(list_g)->push_back (task);
//}
//
//tasks_t
//TaskPane :: get_selected_tasks () const
//{
//  tasks_t tasks;
//  GtkTreeView * view (GTK_TREE_VIEW (_view));
//  GtkTreeSelection * sel (gtk_tree_view_get_selection (view));
//  gtk_tree_selection_selected_foreach (sel, get_selected_tasks_foreach, &tasks);
//  return tasks;
//}
