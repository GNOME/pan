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

#ifndef __PanFileEntry_h__
#define __PanFileEntry_h__

#include <gtk/gtk.h>
#include <gdk/gdk.h>

namespace pan
{
  GtkWidget*    file_entry_new (const char * title,
                                int chooser_mode = GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER);

  GtkWidget*    file_entry_gtk_entry (GtkWidget * w);

  void          file_entry_set (GtkWidget * w, const char * file);

  const char*   file_entry_get (GtkWidget * w);
};

#endif
