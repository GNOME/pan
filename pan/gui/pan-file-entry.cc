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

#include <config.h>
#include <string>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/file-util.h>
#include "pad.h"
#include "pan-file-entry.h"


namespace
{
  void
  entry_clicked_cb (GtkWidget * button, gpointer user_data)
  {
    // create the dialog
    const char * title = (const char*) g_object_get_data (G_OBJECT(user_data), "title");
    const int action = GPOINTER_TO_INT (g_object_get_data(G_OBJECT(user_data), "chooser-action"));
    GtkWidget * w = gtk_file_chooser_dialog_new (title,
                                                 GTK_WINDOW(gtk_widget_get_toplevel(button)),
                                                 GtkFileChooserAction(action),
                                                 GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                 GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                                 nullptr);
    gtk_dialog_set_default_response (GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);

    std::string text (pan::file_entry_get (GTK_WIDGET(user_data)));
    GtkFileChooser * chooser (GTK_FILE_CHOOSER (w));
    const bool file_mode (action==GTK_FILE_CHOOSER_ACTION_OPEN || action==GTK_FILE_CHOOSER_ACTION_SAVE);
    if (file_mode)
        gtk_file_chooser_set_filename (chooser, text.c_str());
    else
        gtk_file_chooser_set_current_folder (chooser, text.c_str());

    const int response (gtk_dialog_run (GTK_DIALOG(w)));
    if (response == GTK_RESPONSE_ACCEPT)
    {
      char * tmp = file_mode
        ? gtk_file_chooser_get_filename (chooser)
        : gtk_file_chooser_get_current_folder (chooser);
      pan :: file_entry_set (GTK_WIDGET(user_data), tmp);
      g_free (tmp);
    }

    gtk_widget_destroy(w);
  }
}

GtkWidget*
pan :: file_entry_new (const char * title, int chooser_action)
{
  // create the widgetry
  GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
  GtkWidget * e = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX(hbox), e, true, true, 0);
  GtkWidget * b = gtk_button_new_from_stock (GTK_STOCK_OPEN);
  g_signal_connect (b, "clicked", G_CALLBACK(entry_clicked_cb), hbox);
  gtk_box_pack_start (GTK_BOX(hbox), b, false, false, 0);

  // add the keys
  g_object_set_data_full (G_OBJECT(hbox), "title", g_strdup(title), g_free);
  g_object_set_data (G_OBJECT(hbox), "chooser-action", GINT_TO_POINTER(chooser_action));
  g_object_set_data (G_OBJECT(hbox), "entry", e);
  file_entry_set (hbox, g_get_home_dir());

  gtk_widget_show (e);
  gtk_widget_show (b);
  return hbox;
}

void
pan :: file_entry_set (GtkWidget * w, const char * file)
{
  GtkEntry * e = GTK_ENTRY(g_object_get_data(G_OBJECT(w), "entry"));
  gtk_entry_set_text (GTK_ENTRY(e), file);
}

const char*
pan :: file_entry_get (GtkWidget * w)
{
  GtkEntry * e = GTK_ENTRY(g_object_get_data(G_OBJECT(w), "entry"));
  return (const char*) gtk_entry_get_text (e);
}

GtkWidget*
pan :: file_entry_gtk_entry (GtkWidget * w)
{
  return GTK_WIDGET (g_object_get_data (G_OBJECT(w), "entry"));
}
