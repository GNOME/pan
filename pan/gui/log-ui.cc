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

#include <config.h>
#include <ostream>
#include <fstream>
extern "C" {
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include "log-ui.h"
#include "pad.h"

using namespace pan;

namespace
{
  enum { COL_SEVERITY, COL_DATE, COL_MESSAGE, N_COLS };

  struct MyLogListener: private Log::Listener
  {
    GtkListStore * myStore;

    MyLogListener (GtkListStore * store): myStore(store) {
      Log::get().add_listener (this);
    }

    ~MyLogListener () {
      Log::get().remove_listener (this);
    }

    virtual void on_log_entry_added (const Log::Entry& e) {
      GtkTreeIter iter;
      gtk_list_store_prepend (myStore, &iter);
      gtk_list_store_set (myStore, &iter, 
                          COL_SEVERITY, (e.severity & Log::PAN_SEVERITY_ERROR),
                          COL_DATE, (unsigned long)e.date,
                          COL_MESSAGE, e.message.c_str(), -1);
    }

    virtual void on_log_cleared () {
      gtk_list_store_clear (myStore);
    }
  };

  void delete_my_log_listener (gpointer object)
  {
    delete (MyLogListener*) object;
  }
}

namespace
{
  void
  log_response_cb (GtkDialog * dialog, int response, gpointer )
  {
    if (response == GTK_RESPONSE_NO)
    {
      Log::get().clear ();
    }
    else if (response == GTK_RESPONSE_CLOSE)
    {
      gtk_widget_destroy (GTK_WIDGET(dialog));
    }
    else if (response == GTK_RESPONSE_APPLY)
    {
      GtkWidget * d = gtk_file_chooser_dialog_new (
        _("Save Event List"),
        GTK_WINDOW(dialog),
        GTK_FILE_CHOOSER_ACTION_SAVE,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
        NULL);
      if (GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(d)))
      {
        char * fname = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (d));
        const Log::entries_t& entries (Log::get().get_entries());
        std::ofstream out (fname, std::ios_base::out|std::ios_base::trunc);
        foreach_const (Log::entries_t, entries, it) {
          StringView date (ctime (&it->date));
          --date.len; // trim off the \n
          out << date << " - " << it->message << '\n';
        }
        out.close ();
        g_free (fname);
      }
      gtk_widget_destroy (d);
    }
  }
}

namespace
{
  GtkListStore*
  create_model ()
  {
    GtkListStore * store = gtk_list_store_new (N_COLS,
                                               G_TYPE_BOOLEAN, // true==error, false==info
                                               G_TYPE_ULONG, // date
                                               G_TYPE_STRING); // message

    const Log::entries_t& entries (Log::get().get_entries());
    foreach_const (Log::entries_t, entries, it) {
      GtkTreeIter iter;
      gtk_list_store_prepend (store, &iter);
      gtk_list_store_set (store, &iter, 
                          COL_SEVERITY, (it->severity & Log::PAN_SEVERITY_ERROR),
                          COL_DATE, (unsigned long)it->date,
                          COL_MESSAGE, it->message.c_str(), -1);
    }

    return store;
  }
}

namespace
{
  void
  render_severity (GtkTreeViewColumn * ,
                   GtkCellRenderer   * renderer,
                   GtkTreeModel      * model,
                   GtkTreeIter       * iter,
                   gpointer            dialog)
  {
    gboolean severe (false);
    gtk_tree_model_get (model, iter, COL_SEVERITY, &severe, -1);
    const char * key (severe ? "pixbuf-error" : "pixbuf-info");
    g_object_set (renderer, "pixbuf", g_object_get_data(G_OBJECT(dialog),key), NULL);
  }

  void
  render_date (GtkTreeViewColumn * ,
               GtkCellRenderer   * renderer,
               GtkTreeModel      * model,
               GtkTreeIter       * iter,
               gpointer            )
  {
    unsigned long date_ul;
    gtk_tree_model_get (model, iter, COL_DATE, &date_ul, -1);
    time_t date_t (date_ul);
    std::string s = ctime (&date_t);
    s.resize (s.size()-1); // remove \n
    g_object_set (renderer, "text", s.c_str(), NULL);
  }
}

GtkWidget*
pan :: log_dialog_new (Prefs& prefs, GtkWindow* window)
{
  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Events"),
                                                    window,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_CLEAR, GTK_RESPONSE_NO,
                                                    GTK_STOCK_SAVE, GTK_RESPONSE_APPLY,
                                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                                    NULL);
  g_signal_connect (dialog, "response", G_CALLBACK(log_response_cb), NULL);

  GtkIconTheme * theme = gtk_icon_theme_get_default ();
  GdkPixbuf * err_pixbuf = gtk_icon_theme_load_icon (theme, GTK_STOCK_DIALOG_ERROR, 20, (GtkIconLookupFlags)0, NULL);
  g_object_set_data_full (G_OBJECT(dialog), "pixbuf-error", err_pixbuf, g_object_unref);
  GdkPixbuf * info_pixbuf = gtk_icon_theme_load_icon (theme, GTK_STOCK_DIALOG_INFO, 20, (GtkIconLookupFlags)0, NULL);
  g_object_set_data_full (G_OBJECT(dialog), "pixbuf-info", info_pixbuf, g_object_unref);

  GtkListStore * store = create_model ();
  GtkWidget * view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
  g_object_set_data_full (G_OBJECT(view), "listener", new MyLogListener(store), delete_my_log_listener);
  GtkWidget * scroll = gtk_scrolled_window_new (0, 0);
  gtk_container_set_border_width (GTK_CONTAINER(scroll), PAD_BIG);
  gtk_container_add (GTK_CONTAINER(scroll), view);

  GtkCellRenderer * pixbuf_renderer = gtk_cell_renderer_pixbuf_new ();
  GtkCellRenderer * text_renderer = gtk_cell_renderer_text_new ();

  // severity
  GtkTreeViewColumn * col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width (col, 24);
  gtk_tree_view_column_set_resizable (col, false);
  gtk_tree_view_column_pack_start (col, pixbuf_renderer, false);
  gtk_tree_view_column_set_cell_data_func (col, pixbuf_renderer, render_severity, dialog, 0);
  gtk_tree_view_column_set_sort_column_id (col, COL_SEVERITY);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);

  // date
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_sort_column_id (col, COL_DATE);
  gtk_tree_view_column_set_title (col, _("Date"));
  gtk_tree_view_column_pack_start (col, text_renderer, false);
  gtk_tree_view_column_set_cell_data_func (col, text_renderer, render_date, 0, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);

  // message
  text_renderer = gtk_cell_renderer_text_new ();
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_sort_column_id (col, COL_MESSAGE);
  gtk_tree_view_column_set_title (col, _("Message"));
  gtk_tree_view_column_pack_start (col, text_renderer, true);
  gtk_tree_view_column_set_attributes (col, text_renderer, "text", COL_MESSAGE, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);

  gtk_widget_show (view);
  gtk_widget_show (scroll);
  pan_box_pack_start_defaults (GTK_BOX(gtk_dialog_get_content_area( GTK_DIALOG(dialog))), scroll);

  gtk_window_set_role (GTK_WINDOW(dialog), "pan-events-dialog");
  prefs.set_window ("events-window", GTK_WINDOW(dialog), 150, 150, 600, 300);
  gtk_window_set_resizable (GTK_WINDOW(dialog), true);
  if (window != 0)
    gtk_window_set_transient_for (GTK_WINDOW(dialog), window);

  return dialog;
}
