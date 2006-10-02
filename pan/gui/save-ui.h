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

#ifndef __SaveUI_h__
#define __SaveUI_h__

#include <string>
#include <vector>
#include <pan/general/quark.h>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <glib.h>
#include <gtk/gtkwindow.h>
#include "group-prefs.h"
#include "prefs.h"

namespace pan
{
  class SaveDialog
  {
    public:
      SaveDialog (Prefs                       & prefs,
                  const GroupPrefs            & group_prefs,
                  const ServerRank            & server_rank,
                  const GroupServer           & group_server,
                  ArticleCache                & cache,
                  ArticleRead                 & read,
                  Queue                       & queue,
                  GtkWindow                   * parent_window,
                  const Quark                 & group,
                  const std::vector<Article>  & articles);


      ~SaveDialog () {}
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
      GtkWidget * _save_text_check;
      GtkWidget * _save_attachments_check;
      GtkWidget * _save_path_entry;
      GtkWidget * _save_custom_path_radio;
      GtkWidget * _save_group_path_radio;
      GtkWidget * _position_age_radio;
      GtkWidget * _position_top_radio;
      GtkWidget * _position_bottom_radio;
      std::vector<Article> _articles;

    private:
      static void response_cb (GtkDialog*, int, gpointer);

    private:
      static bool _save_text;
      static bool _save_attachments;
  };
} 
#endif
