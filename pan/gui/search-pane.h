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


#ifndef _SearchPane_h_
#define _SearchPane_h_

#include <pan/data/data.h>
#include <pan/gui/gui.h>
#include <pan/gui/action-manager.h>
#include <pan/tasks/queue.h>
#include <pan/general/e-util.h>

#include "gtk-compat.h"

namespace pan
{
  class FeedItem
  {
    public:
      std::string title;
      std::string url;
      std::string date;
      std::string desc;
      FeedItem(const char*, const char*, const char*, time_t);
  };

  class SearchPane
  {

    private:
      Data& _data;
      Queue& _queue;
      GtkWidget* _root;
      GtkWidget * _view;
      GtkListStore * _store;
      GtkUIManager* _uim;
      GUI& _gui;
      ActionManager& _action_manager;
      GtkWidget* _web_view;

      typedef std::vector<FeedItem*> download_v;

    public:
       SearchPane (Data&, Queue&, GUI&, ActionManager&);
       ~SearchPane ();
       void refresh();

       GtkWidget* view () { return _view; }
       GtkListStore* store () { return _store; }

    public:
       GtkWidget* root () { return _root; }
       void add_actions(GtkWidget*);
       static void download_clicked_cb (GtkButton*, SearchPane*);
       static void refresh_clicked_cb (GtkButton*, SearchPane*);

       static void do_popup_menu (GtkWidget*, GdkEventButton *event, gpointer pane_g);
       static gboolean on_button_pressed (GtkWidget * treeview, GdkEventButton *event, gpointer userdata);
       static void get_selected_downloads_foreach (GtkTreeModel*, GtkTreePath*, GtkTreeIter*, gpointer);
       FeedItem* get_selection();
       GtkWidget* create_filter_entry ();
       void filter (const std::string& text, int mode);
  };

}

#endif


