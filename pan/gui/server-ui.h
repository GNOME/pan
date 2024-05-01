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

#ifndef SERVER_UI_H
#define SERVER_UI_H

#include <set>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/data/data.h>
#include <pan/tasks/queue.h>

namespace pan
{

  typedef std::set<std::string> strings_t;

  /** @ingroup GUI */
  GtkWidget* server_edit_dialog_new (Data&, Queue&, Prefs&, GtkWindow*, const Quark& server);

  /** @ingroup GUI */
  GtkWidget* server_list_dialog_new (Data&, Queue&, Prefs&, GtkWindow*);

  /** @ingroup GUI */
  GtkWidget* sec_dialog_new (Data& data, Queue& queue, Prefs&, GtkWindow* parent);

  /** @ingroup GUI */
  std::string
  import_sec_from_disk_dialog_new (Data& data, Queue& queue, GtkWindow * window);

  /** @ingroup GUI */
  void render_cert_flag (GtkTreeViewColumn * ,
                         GtkCellRenderer   * ,
                         GtkTreeModel      * ,
                         GtkTreeIter       * ,
                         gpointer            );
}

#endif
