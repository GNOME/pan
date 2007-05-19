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
#include <cstdlib>
#include <cstring>
extern "C" {
  #include <glib.h>
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/general/foreach.h>
#include <pan/general/quark.h>
#include <pan/data/data.h>
#include "server-ui.h"
#include "pad.h"
#include "hig.h"

using namespace pan;

/************
*************  EDIT DIALOG
************/

namespace
{
  struct ServerEditDialog
  {
    Data& data;
    Queue& queue;
    Quark server;
    GtkWidget * dialog;
    GtkWidget * address_entry;
    GtkWidget * port_spin;
    GtkWidget * auth_username_entry;
    GtkWidget * auth_password_entry;
    GtkWidget * connection_limit_spin;
    GtkWidget * expiration_age_combo;
    GtkWidget * rank_combo;
    ServerEditDialog (Data& d, Queue& q): data(d), queue(q) {}
  };

  void pan_entry_set_text (GtkWidget * w, const StringView& v)
  {
    GtkEditable * e (GTK_EDITABLE(w));
    gtk_editable_delete_text (e, 0, -1);
    if (!v.empty())
    {
      gint pos (0);
      gtk_editable_insert_text (e, v.str, v.len,  &pos);
    }
  }

  StringView pan_entry_get_text (GtkWidget * w)
  {
    GtkEntry * entry (GTK_ENTRY (w));
    return StringView (gtk_entry_get_text(entry));
  }

  void pan_spin_button_set (GtkWidget * w, int i)
  {
    GtkSpinButton * sb (GTK_SPIN_BUTTON (w));
    GtkAdjustment * a (gtk_spin_button_get_adjustment (sb));
    gtk_adjustment_set_value (a, i);
  }

  void
  edit_dialog_populate (Data& data, const Quark& server, ServerEditDialog * d)
  {
    // sanity clause
    g_return_if_fail (d!=0);
    g_return_if_fail (GTK_IS_WIDGET(d->dialog));

    d->server = server;

    int port(119), max_conn(4), age(31*3), rank(1);
    std::string addr, user, pass;
    if (!server.empty()) {
      d->data.get_server_addr (server, addr, port);
      d->data.get_server_auth (server, user, pass);
      age = d->data.get_server_article_expiration_age (server);
      rank = d->data.get_server_rank (server);
      max_conn = d->data.get_server_limits (server);
    }

    pan_entry_set_text (d->address_entry, addr);
    pan_spin_button_set (d->port_spin, port);
    pan_spin_button_set (d->connection_limit_spin, max_conn);
    pan_entry_set_text (d->auth_username_entry, user);
    pan_entry_set_text (d->auth_password_entry, pass);

    // set the age combobox
    GtkComboBox * combo (GTK_COMBO_BOX (d->expiration_age_combo));
    GtkTreeModel * model (gtk_combo_box_get_model (combo));
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first(model, &iter)) do {
      int that;
      gtk_tree_model_get (model, &iter, 1, &that, -1);
      if (that == age) {
        gtk_combo_box_set_active_iter (combo, &iter);
        break;
      }
    } while (gtk_tree_model_iter_next(model, &iter));

    // set the rank combo
    combo = GTK_COMBO_BOX (d->rank_combo);
    model = gtk_combo_box_get_model (combo);
    if (gtk_tree_model_get_iter_first(model, &iter)) do {
      int that;
      gtk_tree_model_get (model, &iter, 1, &that, -1);
      if (that == rank) {
        gtk_combo_box_set_active_iter (combo, &iter);
        break;
      }
    } while (gtk_tree_model_iter_next(model, &iter));
  }

  void
  server_edit_response_cb (GtkDialog * w, int response, gpointer user_data)
  {
    bool destroy (true);

    ServerEditDialog * d (static_cast<ServerEditDialog*>(user_data));
    g_assert (d!=NULL);
    g_assert (GTK_IS_WIDGET(d->dialog));

    if (response == GTK_RESPONSE_OK)
    {
      StringView addr (pan_entry_get_text (d->address_entry));
      addr.trim ();
      const int port (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(d->port_spin)));
      const int max_conn (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(d->connection_limit_spin)));
      StringView user (pan_entry_get_text (d->auth_username_entry));
      StringView pass (pan_entry_get_text (d->auth_password_entry));
      int age (31);
      GtkTreeIter iter;
      GtkComboBox * combo (GTK_COMBO_BOX (d->expiration_age_combo));
      if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model(combo), &iter, 1, &age, -1);
      int rank (1);
      combo = GTK_COMBO_BOX (d->rank_combo);
      if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model(combo), &iter, 1, &rank, -1);
      const char * err_msg (0);
      if (addr.empty())
        err_msg = _("Please specify the server's address.");

      if (err_msg) {
        GtkWidget * dialog (gtk_message_dialog_new (GTK_WINDOW(d->dialog),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_CLOSE,
                                                    err_msg));
        g_signal_connect_swapped (dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show (dialog);
        destroy = false;
      } else {
        if (d->server.empty())
          d->server = d->data.add_new_server ();
        d->data.set_server_addr (d->server, addr, port);
        d->data.set_server_auth (d->server, user, pass);
        d->data.set_server_limits (d->server, max_conn);
        d->data.set_server_article_expiration_age (d->server, age);
        d->data.set_server_rank (d->server, rank);
        d->queue.upkeep ();
      }
    }

    if (destroy)
      gtk_widget_destroy (GTK_WIDGET(w));
  }

  void delete_server_edit_dialog (gpointer p)
  {
    delete (ServerEditDialog*)p;
  }
}

GtkWidget*
pan :: server_edit_dialog_new (Data& data, Queue& queue, GtkWindow * window, const Quark& server)
{
  ServerEditDialog * d (new ServerEditDialog (data, queue));

  // create the dialog
  char * title = g_strdup_printf ("Pan: %s", server.empty() ? _("Add a Server") : _("Edit a Server's Settings"));
  GtkWidget * dialog = gtk_dialog_new_with_buttons (title,
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                                    NULL);
  g_free (title);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-edit-server-dialog");
  d->dialog = dialog;
  g_object_set_data_full (G_OBJECT(dialog), "dialog", d, delete_server_edit_dialog);
  g_signal_connect (dialog, "response", G_CALLBACK(server_edit_response_cb), d);

  /**
  ***  workarea
  **/

  GtkTooltips * ttips = gtk_tooltips_new ();
  g_object_ref_sink_pan (G_OBJECT(ttips));
  g_object_weak_ref (G_OBJECT(dialog), (GWeakNotify)g_object_unref, ttips);


  int row (0);
  GtkWidget * t (HIG::workarea_create ());
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(d->dialog)->vbox), t, TRUE, TRUE, 0);
  HIG::workarea_add_section_title (t, &row, _("Location"));
    HIG::workarea_add_section_spacer (t, row, 2);

    GtkWidget * w = d->address_entry = gtk_entry_new ();
    gtk_tooltips_set_tip (GTK_TOOLTIPS(ttips), w, _("The news server's actual address, e.g. \"news.mynewsserver.com\""), NULL);
    HIG::workarea_add_row (t, &row, _("_Address:"), w, NULL);

    GtkAdjustment * a = GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 1.0, ULONG_MAX, 1.0, 1.0, 1.0));
    w = d->port_spin = gtk_spin_button_new (GTK_ADJUSTMENT(a), 1.0, 0u);
    gtk_tooltips_set_tip (GTK_TOOLTIPS(ttips), w, _("The news server's port number.  Typically 119."), NULL);
    HIG::workarea_add_row (t, &row, _("Por_t:"), w, NULL);

  HIG::workarea_add_section_divider (t, &row);
  HIG::workarea_add_section_title (t, &row, _("Login (if Required)"));
    HIG::workarea_add_section_spacer (t, row, 2);

    w = d->auth_username_entry = gtk_entry_new ();
    HIG::workarea_add_row (t, &row, _("_Username:"), w, NULL);
    gtk_tooltips_set_tip (GTK_TOOLTIPS(ttips), w, _("The username to give the server when asked.  If your server doesn't require authentication, you can leave this blank."), NULL);

    w = d->auth_password_entry = gtk_entry_new ();
    gtk_entry_set_visibility (GTK_ENTRY(w), FALSE);
    HIG::workarea_add_row (t, &row, _("_Password:"), w, NULL);
    gtk_tooltips_set_tip (GTK_TOOLTIPS(ttips), w, _("The password to give the server when asked.  If your server doesn't require authentication, you can leave this blank."), NULL);

  HIG::workarea_add_section_divider (t, &row);
  HIG::workarea_add_section_title (t, &row, _("Settings"));
    HIG::workarea_add_section_spacer (t, row, 2);

    const int DEFAULT_MAX_PER_SERVER (4);
    a = GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 0.0, DEFAULT_MAX_PER_SERVER, 1.0, 1.0, 1.0));
    d->connection_limit_spin = w = gtk_spin_button_new (GTK_ADJUSTMENT(a), 1.0, 0u);
    HIG::workarea_add_row (t, &row, _("Connection _Limit:"), w, NULL);

    struct { int type; const char * str; } items[] = {
      { 14,  N_("After Two Weeks") },
      { 31,  N_("After One Month") },
      { (31*2),  N_("After Two Months") },
      { (31*3),  N_("After Three Months") },
      { (31*6),  N_("After Six Months") },
      { 0,   N_("Never Expire Old Articles") }
    };
    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, 0, _(items[i].str), 1, items[i].type, -1);
    }
    d->expiration_age_combo = w = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (G_OBJECT(store));
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, NULL);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    HIG::workarea_add_row (t, &row, _("E_xpire Old Articles:"), w, NULL);

    struct { int rank; const char * str; } rank_items[] = {
      { 1, N_("Primary") },
      { 2, N_("Fallback") }
    };
    store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(rank_items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, 0, _(rank_items[i].str), 1, rank_items[i].rank, -1);
    }

    d->rank_combo = w = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (G_OBJECT(store));
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, true);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, NULL);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    GtkWidget * l = gtk_label_new (_("Server Rank:"));
    GtkWidget * e = gtk_event_box_new ();
    gtk_container_add (GTK_CONTAINER(e), l);
    gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
    gtk_tooltips_set_tip (GTK_TOOLTIPS(ttips), e, _("Fallback servers are used for articles that can't be found on the primaries.  One common approach is to use free servers as primaries and subscription servers as fallbacks."), NULL);
    HIG::workarea_add_row (t, &row, e, w);

  d->server = server;
  edit_dialog_populate (data, server, d);
  gtk_widget_show_all (d->dialog);
  return d->dialog;
}


/************
*************  LIST DIALOG
************/

namespace
{
  enum
  {
    COL_HOST,
    COL_DATA,
    N_COLUMNS
  };

  struct ServerListDialog
  {
    Data& data;
    Queue& queue;
    GtkWidget * server_tree_view;
    GtkWidget * dialog;
    GtkListStore * servers_store;
    GtkWidget * remove_button;
    GtkWidget * edit_button;
    ServerListDialog (Data& d, Queue& q): data(d), queue(q) {}
  };

  Quark
  get_selected_server (ServerListDialog * d)
  {
    g_assert (d != 0);

    Quark server;

    GtkTreeSelection * selection (gtk_tree_view_get_selection(GTK_TREE_VIEW (d->server_tree_view)));
    GtkTreeModel * model;
    GtkTreeIter iter;
    if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
      char * host (0);
      gtk_tree_model_get (model, &iter, COL_DATA, &host, -1);
      if (host) {
        server = host;
        g_free (host);
      }
    }

    //std::cerr << LINE_ID << " selected server is " << server << std::endl;
    return server;
  }

  void
  button_refresh (ServerListDialog * d)
  {
    const bool have_sel (!get_selected_server(d).empty());
    gtk_widget_set_sensitive (d->edit_button, have_sel);
    gtk_widget_set_sensitive (d->remove_button, have_sel);
  }

  void
  server_tree_view_refresh (ServerListDialog * d)
  {
    GtkTreeSelection * selection (gtk_tree_view_get_selection(GTK_TREE_VIEW (d->server_tree_view)));
    const quarks_t servers (d->data.get_servers ());
    const Quark selected_server (get_selected_server (d));

    bool found_selected (false);
    GtkTreeIter selected_iter;
    gtk_list_store_clear (d->servers_store);
    foreach_const (quarks_t, servers, it)
    {
      const Quark& server (*it);
      int port;
      std::string addr;
      d->data.get_server_addr (*it, addr, port);

      GtkTreeIter iter;
      gtk_list_store_append (d->servers_store, &iter);
      gtk_list_store_set (d->servers_store, &iter,
                          COL_HOST, addr.c_str(),
                          COL_DATA, server.c_str(),
                          -1);
      if ((found_selected = (server == selected_server)))
        selected_iter = iter;
    }

    if (found_selected)
      gtk_tree_selection_select_iter (selection, &selected_iter);
  }

  void delete_server_list_dialog (gpointer p)
  {
    delete (ServerListDialog*)p;
  }

  void
  server_list_dialog_response_cb (GtkDialog * dialog, int response, gpointer user_data)
  {
    gtk_widget_destroy (GTK_WIDGET(dialog));
  }

  void
  remove_button_clicked_cb (GtkButton * button, gpointer data)
  {
    ServerListDialog * d (static_cast<ServerListDialog*>(data));
    Quark selected_server (get_selected_server (d));
    if (!selected_server.empty())
    {
      int port;
      std::string addr;
      d->data.get_server_addr (selected_server, addr, port);

      GtkWidget * w = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
                                              GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
                                              GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_NONE,
                                              _("Really delete \"%s\"?"), 
                                              addr.c_str());
      gtk_dialog_add_buttons (GTK_DIALOG(w),
                              GTK_STOCK_NO, GTK_RESPONSE_NO,
                              GTK_STOCK_DELETE, GTK_RESPONSE_YES,
                              NULL);
      gtk_dialog_set_default_response (GTK_DIALOG(w), GTK_RESPONSE_NO);
      const int response (gtk_dialog_run (GTK_DIALOG (w)));
      gtk_widget_destroy (w);
      if (response == GTK_RESPONSE_YES)
        d->data.delete_server (selected_server);

      server_tree_view_refresh (d);
      button_refresh (d);
    }
  }

  void
  server_edit_dialog_destroy_cb (GtkWidget * w, gpointer user_data)
  {
    if (GTK_IS_WIDGET (user_data))
    {
      ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(user_data), "dialog");
      server_tree_view_refresh (d);
    }
  }

  void
  add_button_clicked_cb (GtkButton * button, gpointer user_data)
  {
    const Quark empty_quark;
    GtkWidget * list_dialog = GTK_WIDGET (user_data);
    ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(list_dialog), "dialog");
    GtkWidget * edit_dialog = server_edit_dialog_new (d->data, d->queue, GTK_WINDOW(list_dialog), empty_quark);
    g_signal_connect (edit_dialog, "destroy", G_CALLBACK(server_edit_dialog_destroy_cb), list_dialog);
    gtk_widget_show_all (edit_dialog);
  }

  void
  edit_button_clicked_cb (GtkButton * button, gpointer user_data)
  {
    GtkWidget * list_dialog = GTK_WIDGET (user_data);
    ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(list_dialog), "dialog");
    Quark selected_server (get_selected_server (d));
    if (!selected_server.empty()) {
      GtkWidget * edit_dialog = server_edit_dialog_new (d->data, d->queue, GTK_WINDOW(list_dialog), selected_server);
      g_signal_connect (GTK_OBJECT(edit_dialog), "destroy", G_CALLBACK(server_edit_dialog_destroy_cb), list_dialog);
      gtk_widget_show_all (edit_dialog);
    }
  }

  void
  server_tree_view_row_activated_cb (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col, gpointer user_data)
  {
    edit_button_clicked_cb (NULL, user_data);	
  }

  void
  server_tree_view_selection_changed_cb (GtkTreeSelection * selection, gpointer user_data)
  {
    button_refresh ((ServerListDialog*)user_data);
  }
}


GtkWidget*
pan :: server_list_dialog_new (Data& data, Queue& queue, GtkWindow* parent)
{
  ServerListDialog * d = new ServerListDialog (data, queue);

  // dialog
  char * title = g_strdup_printf ("Pan: %s", _("Servers"));
  GtkWidget * w = d->dialog = gtk_dialog_new_with_buttons (title, parent,
                                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                                           GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
                                                           NULL);
  g_free (title);
  gtk_window_set_role (GTK_WINDOW(w), "pan-servers-dialog");
  gtk_window_set_policy (GTK_WINDOW(w), TRUE, TRUE, TRUE);
  g_signal_connect (GTK_OBJECT(w), "response", G_CALLBACK(server_list_dialog_response_cb), d);
  g_object_set_data_full (G_OBJECT(w), "dialog", d, delete_server_list_dialog);

  GtkTooltips * tips = gtk_tooltips_new ();
  g_object_ref_sink_pan (G_OBJECT(tips));
  g_object_weak_ref (G_OBJECT(w), (GWeakNotify)g_object_unref, tips);

  // workarea
  GtkWidget * hbox = gtk_hbox_new (FALSE, PAD);
  gtk_container_set_border_width (GTK_CONTAINER(hbox), 12);
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(w)->vbox), hbox, TRUE, TRUE, 0);


  // create the list
  d->servers_store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING);
  w = d->server_tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->servers_store));
  GtkCellRenderer * renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn * column = gtk_tree_view_column_new_with_attributes (_("Servers"), renderer, "text", COL_HOST, NULL);
  gtk_tree_view_column_set_sort_column_id (column, COL_HOST);
  gtk_tree_view_append_column (GTK_TREE_VIEW (d->server_tree_view), column);
  GtkTreeSelection * selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (d->server_tree_view));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);
	
  // add callbacks
  g_signal_connect (GTK_TREE_VIEW (d->server_tree_view), "row-activated",
                    G_CALLBACK (server_tree_view_row_activated_cb), d->dialog);
  g_signal_connect (G_OBJECT (selection), "changed",
                    G_CALLBACK (server_tree_view_selection_changed_cb), d);

  w = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), d->server_tree_view);
  gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);
  gtk_widget_set_size_request (w, 300, 300);

  // button box
  GtkWidget * bbox = gtk_vbox_new (FALSE, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 0);

  // add button
  w = gtk_button_new_from_stock (GTK_STOCK_ADD);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_tooltips_set_tip (tips, w, _("Add a Server"), NULL);
  g_signal_connect (w, "clicked", G_CALLBACK(add_button_clicked_cb), d->dialog);

  // edit button
#if GTK_CHECK_VERSION(2,6,0)
  w = gtk_button_new_from_stock (GTK_STOCK_EDIT);
#else
  w = gtk_button_new_from_stock (GTK_STOCK_OPEN);
#endif
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_tooltips_set_tip (tips, w, _("Edit a Server's Settings"), NULL);
  g_signal_connect (w, "clicked", G_CALLBACK(edit_button_clicked_cb), d->dialog);
  d->edit_button = w;

  // remove button
  w = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_tooltips_set_tip (tips, w, _("Remove a Server"), NULL);
  g_signal_connect (w, "clicked", G_CALLBACK(remove_button_clicked_cb), d);
  d->remove_button = w;

  server_tree_view_refresh (d);
  button_refresh (d);
  return d->dialog;
}
