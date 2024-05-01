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
#ifndef _ScoreViewUI_h_
#define _ScoreViewUI_h_

#include <vector>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/scorefile.h>
#include <pan/data/article.h>
#include <pan/data/data.h>

namespace pan
{
  class Scorefile;

  /**
   * Dialog that shows what Scorefile entries apply to a given Article.
   * @ingroup GUI
   */
  class ScoreView
  {
    public:
      ScoreView (Data& data, GtkWindow* parent,
                 const Quark& group,
                 const Article& article);
      ~ScoreView () {}

    public:
      GtkWidget* root() { return _root; }

    private:
      Data& _data;
      const Quark _group;
      const Article _article;
      GtkWidget * _root;
      GtkWidget * tree_view;
      GtkListStore * _store;
      typedef Scorefile::items_t items_t;
      items_t _items;

    private:
      void tree_view_refresh ();

    private:
      static void remove_clicked_cb (GtkWidget*, gpointer);
      void on_remove ();
      static void add_clicked_cb (GtkWidget*, gpointer);
      void on_add ();
      static void add_destroy_cb (GtkWidget*, gpointer);
  };
}

#endif
