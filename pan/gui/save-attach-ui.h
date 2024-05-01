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

#ifndef __SaveAttachUI_h__
#define __SaveAttachUI_h__

#include <string>
#include <vector>
#include <pan/general/quark.h>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <pan/tasks/task-article.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "group-prefs.h"
#include "prefs.h"

namespace pan
{
  class SaveAttachmentsDialog
  {
    public:

      SaveAttachmentsDialog (Prefs            & prefs,
                  const GroupPrefs            & group_prefs,
                  const ServerRank            & server_rank,
                  const GroupServer           & group_server,
                  ArticleCache                & cache,
                  ArticleRead                 & read,
                  Queue                       & queue,
                  GtkWindow                   * parent_window,
                  const Quark                 & group,
                  const std::vector<Article>  & articles,
                  const TaskArticle::SaveOptions & options,
                  const char                  * filename=nullptr
                  );


      ~SaveAttachmentsDialog () {}
      GtkWidget * root() { return _root; }

    private:
      Prefs& _prefs;
      const ServerRank& _server_rank;
      const GroupServer& _group_server;
      ArticleCache& _cache;
      ArticleRead& _read;
      Queue& _queue;
      const Quark _group;
      GtkWidget * _root;
      GtkWidget * _save_path_entry;
      GtkWidget * _save_custom_path_radio;
      GtkWidget * _save_group_path_radio;
      std::vector<Article> _articles;
      TaskArticle::SaveOptions _options;
      const char* _filename;

    private:
      static void response_cb (GtkDialog*, int, gpointer);

    private:
      static bool _save_text;
      static bool _save_attachments;
  };
}
#endif
