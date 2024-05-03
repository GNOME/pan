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

#include "log-ui.h"

#include <config.h>
#include <ostream>
#include <fstream>
#include <iostream>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include "pad.h"


namespace pan {

namespace
{
  enum { COL_HIDDEN, COL_SEVERITY, COL_DATE, COL_MESSAGE, N_COLS };

  struct MyLogListener: private Log::Listener
  {
    GtkTreeStore * myStore;

    MyLogListener (GtkTreeStore * store): myStore(store) {
      Log::get().add_listener (this);
    }

    ~MyLogListener () {
      Log::get().remove_listener (this);
    }

    virtual void on_log_entry_added (const Log::Entry& e) {
      GtkTreeIter iter;
      gtk_tree_store_prepend (myStore, &iter, nullptr);
      gtk_tree_store_set (myStore, &iter,
                          COL_HIDDEN, "",
                          COL_SEVERITY, (e.severity & Log::PAN_SEVERITY_ERROR),
                          COL_DATE, (unsigned long)e.date,
                          COL_MESSAGE, &e, -1);
       if (!e.messages.empty())
       {
        GtkTreeIter child;

        foreach_const (Log::entries_p, e.messages, lit)
        {
          Log::Entry entry(**lit);
          gtk_tree_store_prepend (myStore, &child, &iter );
          gtk_tree_store_set (myStore, &child,
                          COL_HIDDEN, "",
                          COL_SEVERITY, (entry.severity & Log::PAN_SEVERITY_ERROR),
                          COL_DATE, (unsigned long)entry.date,
                          COL_MESSAGE, &*lit, -1);
        }
      }
    }

    void on_log_cleared () override {
      gtk_tree_store_clear (myStore);
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
        nullptr);
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
  std::string to_string(std::deque<Log::Entry> d)
  {
    std::string tmp;
    foreach_const(std::deque<Log::Entry>, d, it)
      tmp += it->message + "\n";
    return tmp;
  }
}

namespace
{
  GtkTreeStore*
  create_model ()
  {
    GtkTreeStore * store = gtk_tree_store_new (N_COLS,
                                               G_TYPE_STRING,
                                               G_TYPE_BOOLEAN, // true==error, false==info
                                               G_TYPE_ULONG, // date
                                               G_TYPE_POINTER); // message

    const Log::entries_t& entries (Log::get().get_entries());
    foreach_const (Log::entries_t, entries, it) {
      GtkTreeIter   top, child, tmp;
      gtk_tree_store_prepend (store, &top, nullptr);
      gtk_tree_store_set (store, &top,
                          COL_HIDDEN, "",
                          COL_SEVERITY, (it->severity & Log::PAN_SEVERITY_ERROR),
                          COL_DATE, (unsigned long)it->date,
                          COL_MESSAGE, &*it, -1);
      if (!it->messages.empty())
      {
        foreach_const (Log::entries_p, it->messages, lit)
        {
          Log::Entry entry (**lit);
          gtk_tree_store_prepend (store, &child, &top );
          gtk_tree_store_set (store, &child,
                          COL_HIDDEN, "",
                          COL_SEVERITY, (entry.severity & Log::PAN_SEVERITY_ERROR),
                          COL_DATE, (unsigned long)entry.date,
                          COL_MESSAGE, &*lit, -1);
        }
      }
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
    g_object_set (renderer, "pixbuf", g_object_get_data(G_OBJECT(dialog),key), nullptr);
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
    g_object_set (renderer, "text", s.c_str(), nullptr);
  }

    void
  render_message (GtkTreeViewColumn * ,
                  GtkCellRenderer   * renderer,
                  GtkTreeModel      * model,
                  GtkTreeIter       * iter,
                  gpointer            )
  {
    Log::Entry* log_entry(nullptr);
    gtk_tree_model_get (model, iter, COL_MESSAGE, &log_entry, -1);
    bool bold (log_entry->is_child);
    g_object_set (renderer,
                  "text", log_entry->message.c_str(),
                  "weight", bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,
                  nullptr);
  }


}

gboolean
on_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  // single click with the right mouse button?
  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
  {
    GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    GtkTreePath * path;
    if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(treeview),
                                       (gint)event->x, (gint)event->y,
                                       &path, nullptr, nullptr, nullptr))
    {
      if (!gtk_tree_selection_path_is_selected (selection, path))
      {
        gtk_tree_selection_unselect_all (selection);
        gtk_tree_selection_select_path (selection, path);
      }
    }
    const bool expanded (gtk_tree_view_row_expanded (GTK_TREE_VIEW(treeview), path));
    if (expanded)
      gtk_tree_view_collapse_row(GTK_TREE_VIEW(treeview),path);
    else
      gtk_tree_view_expand_row (GTK_TREE_VIEW(treeview),path,false);
    gtk_tree_path_free (path);
    return true;
  }
  return false;
}

GtkWidget*
log_dialog_new (Prefs& prefs, GtkWindow* window)
{
  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Events"),
                                                    window,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_CLEAR, GTK_RESPONSE_NO,
                                                    GTK_STOCK_SAVE, GTK_RESPONSE_APPLY,
                                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                                    nullptr);
  g_signal_connect (dialog, "response", G_CALLBACK(log_response_cb), nullptr);

  GtkIconTheme * theme = gtk_icon_theme_get_default ();
  GdkPixbuf * err_pixbuf = gtk_icon_theme_load_icon (theme, GTK_STOCK_DIALOG_ERROR, 20, (GtkIconLookupFlags)0, nullptr);
  g_object_set_data_full (G_OBJECT(dialog), "pixbuf-error", err_pixbuf, g_object_unref);
  GdkPixbuf * info_pixbuf = gtk_icon_theme_load_icon (theme, GTK_STOCK_DIALOG_INFO, 20, (GtkIconLookupFlags)0, nullptr);
  g_object_set_data_full (G_OBJECT(dialog), "pixbuf-info", info_pixbuf, g_object_unref);

  GtkTreeStore * store = create_model ();
  GtkWidget * view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));

  gtk_tree_view_set_show_expanders(GTK_TREE_VIEW(view),false);

  g_object_set_data_full (G_OBJECT(view), "listener", new MyLogListener(store), delete_my_log_listener);
  GtkWidget * scroll = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_container_set_border_width (GTK_CONTAINER(scroll), PAD_BIG);
  gtk_container_add (GTK_CONTAINER(scroll), view);

  GtkCellRenderer * pixbuf_renderer = gtk_cell_renderer_pixbuf_new ();
  GtkCellRenderer * text_renderer = gtk_cell_renderer_text_new ();

  /* placeholder for expander */
  GtkTreeViewColumn * col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_resizable (col, false);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);
  gtk_tree_view_column_set_visible(col,false);
  gtk_tree_view_set_expander_column(GTK_TREE_VIEW(view), col);

  // severity
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
  gtk_tree_view_column_set_fixed_width (col, 35);
  gtk_tree_view_column_set_resizable (col, false);
  gtk_tree_view_column_pack_start (col, pixbuf_renderer, false);
  gtk_tree_view_column_set_cell_data_func (col, pixbuf_renderer, render_severity, dialog, nullptr);
  gtk_tree_view_column_set_sort_column_id (col, COL_SEVERITY);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);

  // date
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_sort_column_id (col, COL_DATE);
  gtk_tree_view_column_set_title (col, _("Date"));
  gtk_tree_view_column_pack_start (col, text_renderer, false);
  gtk_tree_view_column_set_cell_data_func (col, text_renderer, render_date, nullptr, nullptr);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);

  // message
  text_renderer = gtk_cell_renderer_text_new ();
  col = gtk_tree_view_column_new ();
  gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
  gtk_tree_view_column_set_sort_column_id (col, COL_MESSAGE);
  gtk_tree_view_column_set_title (col, _("Message"));
  gtk_tree_view_column_pack_start (col, text_renderer, true);
  gtk_tree_view_column_set_cell_data_func (col, text_renderer, render_message, nullptr, nullptr);
  gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);
  gtk_tree_view_set_expander_column(GTK_TREE_VIEW(view), col);

  gtk_widget_show (view);
  gtk_widget_show (scroll);
  pan_box_pack_start_defaults (GTK_BOX(gtk_dialog_get_content_area( GTK_DIALOG(dialog))), scroll);

  gtk_window_set_role (GTK_WINDOW(dialog), "pan-events-dialog");
  prefs.set_window ("events-window", GTK_WINDOW(dialog), 150, 150, 600, 300);
  gtk_window_set_resizable (GTK_WINDOW(dialog), true);
  if (window != nullptr)
    gtk_window_set_transient_for (GTK_WINDOW(dialog), window);

  g_signal_connect (view, "button-press-event", G_CALLBACK(on_button_pressed), view);

  return dialog;
}

}
