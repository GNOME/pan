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

#include "server-ui.h"

#include <config.h>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include "pan/gui/load-icon.h"
#include <pan/data/data.h>
#include <pan/usenet-utils/ssl-utils.h>
#include "pad.h"
#include "hig.h"

#ifdef HAVE_GNUTLS
  #include <pan/data/cert-store.h>
  #include <gnutls/gnutls.h>
#endif

#ifdef HAVE_GKR
  #define GCR_API_SUBJECT_TO_CHANGE
  #include <libsecret/secret.h>
  #include <gcr/gcr.h>
  #undef GCR_API_SUBJECT_TO_CHANGE
  #define USE_LIBSECRET_DEFAULT true
#else
  #define USE_LIBSECRET_DEFAULT false
#endif


namespace pan {

/************
*************  EDIT DIALOG
************/


namespace
{
  struct ServerEditDialog
  {
    Data& data;
    Queue& queue;
    Prefs& prefs;
    Quark server;
    StringView cert;
    GtkWidget * dialog;
    GtkWidget * address_entry;
    GtkWidget * port_spin;
    GtkWidget * auth_username_entry;
    GtkWidget * auth_password_entry;
    GtkWidget * connection_limit_spin;
    GtkWidget * expiration_age_combo;
    GtkWidget * rank_combo;
    GtkWidget * compression_combo;
    GtkWidget * ssl_combo;
    GtkWidget * always_trust_checkbox;
    CompressionType compressiontype;

    ServerEditDialog (Data& d, Queue& q, Prefs& p):
      data(d),
      queue(q),
      prefs(p),
      dialog(nullptr),
      address_entry(nullptr),
      port_spin(nullptr),
      auth_username_entry(nullptr),
      auth_password_entry(nullptr),
      connection_limit_spin(nullptr),
      expiration_age_combo(nullptr),
      rank_combo(nullptr),
      compression_combo(nullptr),
      ssl_combo(nullptr),
      always_trust_checkbox(nullptr),
      compressiontype(HEADER_COMPRESS_NONE)
    {}

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

  void ssl_changed_cb(GtkComboBox* w, ServerEditDialog* d)
  {
    int ssl(0);
#ifdef HAVE_GNUTLS
    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter (w, &iter))
      gtk_tree_model_get (gtk_combo_box_get_model(w), &iter, 1, &ssl, -1);
    if (ssl==0)
    {
      gtk_widget_hide(d->always_trust_checkbox);
    }
    else
    {
      gtk_widget_show(d->always_trust_checkbox);
    }
    pan_spin_button_set (d->port_spin, ssl == 0 ? STD_NNTP_PORT : STD_SSL_PORT);
#endif
  }

  void
  edit_dialog_populate (Data&, Prefs& prefs, const Quark& server, ServerEditDialog * d)
  {
    // sanity clause
    g_return_if_fail (d!=nullptr );
    g_return_if_fail (GTK_IS_WIDGET(d->dialog));

    d->server = server;

    int port(STD_NNTP_PORT), max_conn(4), age(31*3), rank(1), ssl(0), trust(0);
    CompressionType compression(HEADER_COMPRESS_NONE);
    std::string addr, user, cert;
    gchar* pass(nullptr);
    if (!server.empty()) {
      d->data.get_server_addr (server, addr, port);
      d->data.get_server_auth (
              server,
              user,
              pass,
              prefs.get_flag(
                  "use-password-storage",
                  USE_LIBSECRET_DEFAULT));
      age = d->data.get_server_article_expiration_age (server);
      rank = d->data.get_server_rank (server);
      max_conn = d->data.get_server_limits (server);
      ssl = d->data.get_server_ssl_support(server);
      cert = d->data.get_server_cert(server);
      d->data.get_server_trust (server, trust);
      d->data.get_server_compression_type (server, compression);
    }

    pan_entry_set_text (d->address_entry, addr);
    pan_spin_button_set (d->port_spin, port);
    pan_spin_button_set (d->connection_limit_spin, max_conn);
    pan_entry_set_text (d->auth_username_entry, user);
    pan_entry_set_text (d->auth_password_entry, pass);
    d->cert = cert;
    d->compressiontype = compression;

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

    //set the compression combobox
    combo = GTK_COMBO_BOX (d->compression_combo);
    model = gtk_combo_box_get_model (combo);
    if (gtk_tree_model_get_iter_first(model, &iter)) do {
      int that;
      gtk_tree_model_get (model, &iter, 1, &that, -1);
      if (that == compression) {
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

#ifdef HAVE_GNUTLS
    // set ssl combo
    combo = GTK_COMBO_BOX (d->ssl_combo);
    model = gtk_combo_box_get_model (combo);
    if (gtk_tree_model_get_iter_first(model, &iter)) do {
      int that;
      gtk_tree_model_get (model, &iter, 1, &that, -1);
      if (that == ssl) {
        gtk_combo_box_set_active_iter (combo, &iter);
        break;
      }
    } while (gtk_tree_model_iter_next(model, &iter));

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(d->always_trust_checkbox), trust);
#endif

    pan_spin_button_set (d->port_spin, port);
  }

  void
  server_edit_response_cb (GtkDialog * w, int response, gpointer user_data)
  {
    bool destroy (true);

    ServerEditDialog * d (static_cast<ServerEditDialog*>(user_data));
    g_assert (d!=nullptr);
    g_assert (GTK_IS_WIDGET(d->dialog));

    if (response == GTK_RESPONSE_OK)
    {
      StringView addr (pan_entry_get_text (d->address_entry));
      addr.trim ();
      const int port (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(d->port_spin)));
      const int max_conn (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(d->connection_limit_spin)));
      StringView user (pan_entry_get_text (d->auth_username_entry));
#ifdef HAVE_GKR
      gchar* pass = gcr_secure_memory_strdup(gtk_entry_get_text(GTK_ENTRY(d->auth_password_entry)));
#else
      gchar* pass = (gchar*)gtk_entry_get_text(GTK_ENTRY(d->auth_password_entry));
#endif
      int age (31);
      GtkTreeIter iter;
      GtkComboBox * combo (GTK_COMBO_BOX (d->expiration_age_combo));
      if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model(combo), &iter, 1, &age, -1);
      int rank (1);
      combo = GTK_COMBO_BOX (d->rank_combo);
      if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model(combo), &iter, 1, &rank, -1);
      int ssl(0);
      int trust(0);

      StringView cert(d->cert);

#ifdef HAVE_GNUTLS
      combo = GTK_COMBO_BOX (d->ssl_combo);
      if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model(combo), &iter, 1, &ssl, -1);
      trust = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->always_trust_checkbox)) ? 1 : 0;
#endif

      int header_comp (HEADER_COMPRESS_NONE);
      combo = GTK_COMBO_BOX (d->compression_combo);
      if (gtk_combo_box_get_active_iter (combo, &iter))
        gtk_tree_model_get (gtk_combo_box_get_model(combo), &iter, 1, &header_comp, -1);

      const char * err_msg (nullptr);
      if (addr.empty())
        err_msg = _("Please specify the server's address.");

      if (err_msg) {
        GtkWidget * dialog (gtk_message_dialog_new (GTK_WINDOW(d->dialog),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_MESSAGE_ERROR,
                                                    GTK_BUTTONS_CLOSE,
                                                    "%s",err_msg));
        g_signal_connect_swapped (dialog, "response", G_CALLBACK(gtk_widget_destroy), dialog);
        gtk_widget_show (dialog);
        destroy = false;
      } else {
        if (d->server.empty())
          d->server = d->data.add_new_server ();
        d->data.set_server_addr (d->server, addr, port);
        d->data.set_server_auth (
                d->server,
                user,
                pass,
                d->prefs.get_flag(
                    "use-password-storage",
                    USE_LIBSECRET_DEFAULT));
        d->data.set_server_limits (d->server, max_conn);
        d->data.set_server_article_expiration_age (d->server, age);
        d->data.set_server_rank (d->server, rank);
        d->data.set_server_ssl_support(d->server, ssl);
        d->data.set_server_cert(d->server,cert);
        d->data.set_server_trust(d->server,trust);
        d->data.set_server_compression_type(d->server, header_comp);
        d->data.save_server_info(d->server);

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

std::string
import_sec_from_disk_dialog_new (Data& data, Queue& queue, GtkWindow * window)
{
  std::string prev_path = g_get_home_dir ();
  std::string res;

  GtkWidget * w = gtk_file_chooser_dialog_new (_("Import SSL Certificate (PEM Format) From File"),
				      window,
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				      nullptr);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (w), prev_path.c_str());
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (w), false);
	gtk_file_chooser_set_show_hidden (GTK_FILE_CHOOSER (w), false);

	const int response (gtk_dialog_run (GTK_DIALOG(w)));
	if (response == GTK_RESPONSE_ACCEPT) {
    res = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (w));
    gtk_widget_destroy (w);
  } else
    gtk_widget_destroy (w);

  return res;
}

namespace
{

  static void server_edit_dialog_realized_cb (GtkWidget*, gpointer gp)
  {
    ServerEditDialog * d (static_cast<ServerEditDialog*>(gp));
    // avoid NPE on early init
    g_signal_connect(d->ssl_combo, "changed", G_CALLBACK(ssl_changed_cb), d);
  }
}

GtkWidget*
server_edit_dialog_new (Data& data, Queue& queue, Prefs& prefs, GtkWindow * window, const Quark& server)
{
  ServerEditDialog * d (new ServerEditDialog (data, queue, prefs));

  // create the dialog
  char * title = g_strdup_printf ("Pan: %s", server.empty() ? _("Add a Server") : _("Edit a Server's Settings"));
  GtkWidget * dialog = gtk_dialog_new_with_buttons (title,
                                                    GTK_WINDOW(window),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                    GTK_STOCK_OK, GTK_RESPONSE_OK,
                                                    nullptr);
  g_free (title);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-edit-server-dialog");
  d->dialog = dialog;
  g_object_set_data_full (G_OBJECT(dialog), "dialog", d, delete_server_edit_dialog);
  g_signal_connect (dialog, "response", G_CALLBACK(server_edit_response_cb), d);

  /**
  ***  workarea
  **/

    int row (0);
    GtkWidget * t (HIG::workarea_create ());
    gtk_box_pack_start (GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG(d->dialog))), t, TRUE, TRUE, 0);
    HIG::workarea_add_section_title (t, &row, _("Location"));
    HIG::workarea_add_section_spacer (t, row, 2);

    GtkWidget * w = d->address_entry = gtk_entry_new ();
    gtk_widget_set_tooltip_text( w, _("The news server's actual address, e.g. \"news.mynewsserver.com\"."));
    HIG::workarea_add_row (t, &row, _("_Address:"), w, nullptr);
    //g_signal_connect (w, "changed", G_CALLBACK(address_entry_changed_cb), d);

    GtkAdjustment * a = GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 1.0, ULONG_MAX, 1.0, 1.0, 0.0));
    w = d->port_spin = gtk_spin_button_new (GTK_ADJUSTMENT(a), 1.0, 0u);
    gtk_widget_set_tooltip_text( w, _("The news server's port number.  Typically 119 for unencrypted and 563 for encrypted connections (SSL/TLS)."));
    HIG::workarea_add_row (t, &row, _("Por_t:"), w, nullptr);

    HIG::workarea_add_section_divider (t, &row);
    HIG::workarea_add_section_title (t, &row, _("Login (if Required)"));
    HIG::workarea_add_section_spacer (t, row, 2);

    w = d->auth_username_entry = gtk_entry_new ();
    HIG::workarea_add_row (t, &row, _("_Username:"), w, nullptr);
    gtk_widget_set_tooltip_text( w, _("The username to give the server when asked.  If your server doesn't require authentication, you can leave this blank."));

    w = d->auth_password_entry = gtk_entry_new ();
    gtk_entry_set_visibility (GTK_ENTRY(w), FALSE);
    HIG::workarea_add_row (t, &row, _("_Password:"), w, nullptr);
    gtk_widget_set_tooltip_text( w, _("The password to give the server when asked.  If your server doesn't require authentication, you can leave this blank."));

    HIG::workarea_add_section_divider (t, &row);
    HIG::workarea_add_section_title (t, &row, _("Settings"));
    HIG::workarea_add_section_spacer (t, row, 2);

    // max connections
    const int DEFAULT_MAX_PER_SERVER (20);
    a = GTK_ADJUSTMENT (gtk_adjustment_new (1.0, 0.0, DEFAULT_MAX_PER_SERVER, 1.0, 1.0, 0.0));
    d->connection_limit_spin = w = gtk_spin_button_new (GTK_ADJUSTMENT(a), 1.0, 0u);
    HIG::workarea_add_row (t, &row, _("Connection _Limit:"), w, nullptr);

    // expiration
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
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    HIG::workarea_add_row (t, &row, _("E_xpire Old Articles:"), w, nullptr);

    //rank
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
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    GtkWidget * l = gtk_label_new (_("Server Rank:"));
    GtkWidget * e = gtk_event_box_new ();
    gtk_container_add (GTK_CONTAINER(e), l);
    gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
    gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
    gtk_widget_set_tooltip_text( e, _("Fallback servers are used for articles that can't be found on the primaries.  One common approach is to use free servers as primaries and subscription servers as fallbacks."));
    HIG::workarea_add_row (t, &row, e, w);

    // header compression list options
    struct { int type; const char * str; } compression_items[] = {
      { HEADER_COMPRESS_NONE, N_("Disable Compression (N/A)") },
      { HEADER_COMPRESS_XZVER, N_("XZVER Compression (Astraweb)") },
      { HEADER_COMPRESS_XFEATURE, N_("GZIP Compression (Giganews etc.)") }
    };
    store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(compression_items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, 0, _(compression_items[i].str), 1, compression_items[i].type, -1);
    }
    d->compression_combo = w = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (G_OBJECT(store));
    renderer =  gtk_cell_renderer_text_new ();
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    HIG::workarea_add_row (t, &row, _("Header Compression:"), w, nullptr);

    // ssl 3.0 option
#ifdef HAVE_GNUTLS
    // select ssl/plaintext
    HIG::workarea_add_section_divider (t, &row);
    HIG::workarea_add_section_title (t, &row, _("Security"));
    HIG::workarea_add_section_spacer (t, row, 2);

    struct { int o; const char * str; } ssl_items[] = {

      { 0, N_("Use Unsecure (Plaintext) Connections") },
      { 1, N_("Use Secure SSL Connections") }
    };

    store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(ssl_items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, 0, _(ssl_items[i].str), 1, ssl_items[i].o, -1);
    }

    d->ssl_combo = w = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    g_object_unref (G_OBJECT(store));
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, true);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    l = gtk_label_new (_("TLS (SSL) Settings:"));
    e = gtk_event_box_new ();
    gtk_container_add (GTK_CONTAINER(e), l);
    gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
    gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
    gtk_widget_set_tooltip_text( e,
          _("You can enable/disable secure SSL/TLS connections here. "
            "If you enable SSL/TLS, your data is encrypted and secure. "
            "It is encouraged to enable SSL/TLS for privacy reasons."));
    HIG::workarea_add_row (t, &row, e, w);

    d->always_trust_checkbox = w = gtk_check_button_new_with_label (_("Always trust this server's certificate"));
    HIG::workarea_add_row (t, &row, nullptr, w, nullptr);
    g_signal_connect (d->dialog, "realize", G_CALLBACK(server_edit_dialog_realized_cb), d);
#endif

  d->server = server;
  edit_dialog_populate (data, prefs, server, d);
  gtk_widget_show_all (d->dialog);

  return d->dialog;
}

namespace
{
  enum
  {
    ICON_PLAIN,
    ICON_CERT,
    ICON_QTY
   };

  struct Icon {
    const char * pixbuf_file;
    GdkPixbuf * pixbuf;
  } _icons[ICON_QTY] = {
    { "icon_plain.png",  nullptr },
    { "icon_cert.png",   nullptr }
  };
}


/************
*************  LIST DIALOG
************/

namespace
{
  enum
  {
    COL_FLAG,
    COL_HOST,
    COL_DATA,
    N_COLUMNS
  };

  struct ServerListDialog
  {
    Data& data;
    Queue& queue;
    Prefs& prefs;
    GtkWidget * server_tree_view;
    GtkWidget * dialog;
    GtkListStore * servers_store;
    GtkWidget * remove_button;
    GtkWidget * edit_button;
    ServerListDialog (Data& d, Queue& q, Prefs& p):
      data(d),
      queue(q),
      prefs(p),
      server_tree_view(nullptr),
      dialog(nullptr),
      servers_store(nullptr),
      remove_button(nullptr),
      edit_button(nullptr)
    {}
  };


  Quark
  get_selected_server (ServerListDialog * d)
  {
    g_assert (d != nullptr);

    Quark server;

    GtkTreeSelection * selection (gtk_tree_view_get_selection(GTK_TREE_VIEW (d->server_tree_view)));
    GtkTreeModel * model;
    GtkTreeIter iter;
    if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
      char * host (nullptr);
      gtk_tree_model_get (model, &iter, COL_DATA, &host, -1);
      if (host) {
        server = host;
        g_free (host);
      }
    }

    //std::cerr << LINE_ID << " selected server is " << server << std::endl;
    return server;
  }

  Quark
  get_selected_server_name (ServerListDialog * d)
  {
    g_assert (d != nullptr);

    Quark server;

    GtkTreeSelection * selection (gtk_tree_view_get_selection(GTK_TREE_VIEW (d->server_tree_view)));
    GtkTreeModel * model;
    GtkTreeIter iter;
    if (gtk_tree_selection_get_selected (selection, &model, &iter)) {
      char * host (nullptr);
      gtk_tree_model_get (model, &iter, COL_HOST, &host, -1);
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
      std::string addr(d->data.get_server_address (*it));

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

  void delete_sec_dialog (gpointer p)
  {
   for (guint i=0; i<ICON_QTY; ++i)
      g_object_unref (_icons[i].pixbuf);
   delete (ServerListDialog*)p;
  }

  void
  server_list_dialog_response_cb (GtkDialog * dialog, int, gpointer)
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
                              nullptr);
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
  server_edit_dialog_destroy_cb (GtkWidget *, gpointer user_data)
  {
    if (GTK_IS_WIDGET (user_data))
    {
      ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(user_data), "dialog");
      server_tree_view_refresh (d);
    }
  }

  void
  add_button_clicked_cb (GtkButton *, gpointer user_data)
  {
    const Quark empty_quark;
    GtkWidget * list_dialog = GTK_WIDGET (user_data);
    ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(list_dialog), "dialog");
    GtkWidget * edit_dialog = server_edit_dialog_new (d->data, d->queue, d->prefs, GTK_WINDOW(list_dialog), empty_quark);
    g_signal_connect (edit_dialog, "destroy", G_CALLBACK(server_edit_dialog_destroy_cb), list_dialog);
    gtk_widget_show_all (edit_dialog);
  }

  void
  edit_button_clicked_cb (GtkButton *, gpointer user_data)
  {
    GtkWidget * list_dialog = GTK_WIDGET (user_data);
    ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(list_dialog), "dialog");
    Quark selected_server (get_selected_server (d));
    if (!selected_server.empty()) {
      GtkWidget * edit_dialog = server_edit_dialog_new (d->data, d->queue, d->prefs, GTK_WINDOW(list_dialog), selected_server);
      g_signal_connect (edit_dialog, "destroy", G_CALLBACK(server_edit_dialog_destroy_cb), list_dialog);
      gtk_widget_show_all (edit_dialog);
    }
  }

  void
  server_tree_view_row_activated_cb (GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer user_data)
  {
    edit_button_clicked_cb (nullptr, user_data);
  }

  void
  server_tree_view_selection_changed_cb (GtkTreeSelection*, gpointer user_data)
  {
    button_refresh ((ServerListDialog*)user_data);
  }
}

#ifdef HAVE_GNUTLS
/* security dialog */
namespace
{

  /* Show the current certificate of the selected server, if any */
  void
  cert_edit_button_clicked_cb (GtkButton *, gpointer user_data)
  {
    GtkWidget * list_dialog = GTK_WIDGET (user_data);
    ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(list_dialog), "dialog");
    Quark selected_server (get_selected_server (d));
    CertStore& store (d->data.get_certstore());

    int port;
    std::string addr;
    d->data.get_server_addr (selected_server, addr, port);

    char buf[4096] ;

    if (!selected_server.empty()) {
      const gnutls_x509_crt_t cert (store.get_cert_to_server(selected_server));
      if (cert)
      {
        pretty_print_x509(buf,sizeof(buf),addr, cert, false);
        if (!buf) g_snprintf(buf,sizeof(buf), "%s", _("No information available.")) ;

        GtkWidget * w = gtk_message_dialog_new (
        nullptr,
        GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_INFO,
        GTK_BUTTONS_CLOSE, nullptr);

        HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(w), buf, nullptr);

        g_snprintf(buf,sizeof(buf), _("Server Certificate for '%s'"), addr.c_str());
        gtk_window_set_title(GTK_WINDOW(w), buf);

        gtk_widget_show_all (w);
        g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
      }
    }
  }

  void
  sec_tree_view_refresh (ServerListDialog * d)
  {
    GtkTreeSelection * selection (gtk_tree_view_get_selection(GTK_TREE_VIEW (d->server_tree_view)));
    const quarks_t servers (d->data.get_servers ());
    const Quark selected_server (get_selected_server (d));
    CertStore& store (d->data.get_certstore());

    bool found_selected (false);
    GtkTreeIter selected_iter;
    gtk_list_store_clear (d->servers_store);
    foreach_const (quarks_t, servers, it)
    {
      const Quark& server (*it);
      std::string addr; int port;
      d->data.get_server_addr (server, addr, port);

      if(d->data.get_server_ssl_support(server))
      {
        GtkTreeIter iter;
        gtk_list_store_append (d->servers_store, &iter);
        gtk_list_store_set (d->servers_store, &iter,
                            COL_FLAG, store.exist(server),
                            COL_HOST, addr.c_str(),
                            COL_DATA, server.c_str(),
                            -1);
        if ((found_selected = (server == selected_server)))
          selected_iter = iter;
      }
    }

    if (found_selected)
      gtk_tree_selection_select_iter (selection, &selected_iter);
  }

  void
  sec_dialog_destroy_cb (GtkWidget *, gpointer user_data)
  {
    if (GTK_IS_WIDGET (user_data))
    {
      ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(user_data), "dialog");
      sec_tree_view_refresh (d);
    }
  }


  /* add a cert from disk, overwriting the current setting for the selected server */
  void
  cert_add_button_clicked_cb (GtkButton *, gpointer user_data)
  {
    const Quark empty_quark;
    GtkWidget * list_dialog = GTK_WIDGET (user_data);
    ServerListDialog * d = (ServerListDialog*) g_object_get_data (G_OBJECT(list_dialog), "dialog");
    std::string ret = import_sec_from_disk_dialog_new (d->data, d->queue, GTK_WINDOW(list_dialog));
    const Quark selected_server (get_selected_server (d));
    CertStore& store (d->data.get_certstore());

    if (!ret.empty() )
    {
      std::string addr; int port;
      d->data.get_server_addr(selected_server, addr, port);
      if (!store.import_from_file(selected_server, ret.c_str()))
      {
      _err:
        Log::add_err_va("Error adding certificate of server '%s' to CertStore. Check the console output!", addr.c_str());
        file::print_file_info(std::cerr,ret.c_str());
      }
      sec_tree_view_refresh (d);
    }
  }


  /* remove cert from certstore */
  void
  cert_remove_button_clicked_cb (GtkButton * button, gpointer data)
  {
    ServerListDialog * d (static_cast<ServerListDialog*>(data));
    Quark selected_server (get_selected_server (d));
    CertStore& store (d->data.get_certstore());

    if (!selected_server.empty())
    {
      int port;
      std::string addr;
      d->data.get_server_addr (selected_server, addr, port);

      GtkWidget * w = gtk_message_dialog_new (GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button))),
                                              GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
                                              GTK_MESSAGE_QUESTION,
                                              GTK_BUTTONS_NONE,
                                              _("Really delete certificate for \"%s\"?"),
                                              addr.c_str());
      gtk_dialog_add_buttons (GTK_DIALOG(w),
                              GTK_STOCK_NO, GTK_RESPONSE_NO,
                              GTK_STOCK_DELETE, GTK_RESPONSE_YES,
                              nullptr);
      gtk_dialog_set_default_response (GTK_DIALOG(w), GTK_RESPONSE_NO);
      const int response (gtk_dialog_run (GTK_DIALOG (w)));
      gtk_widget_destroy (w);
      store.remove(selected_server);

      if (response == GTK_RESPONSE_YES)
        sec_tree_view_refresh (d);

      button_refresh (d);
    }
  }

}
#endif

GtkWidget*
server_list_dialog_new (Data& data, Queue& queue, Prefs& prefs, GtkWindow* parent)
{
  ServerListDialog * d = new ServerListDialog (data, queue, prefs);

  // dialog
  char * title = g_strdup_printf ("Pan: %s", _("Servers"));
  GtkWidget * w = d->dialog = gtk_dialog_new_with_buttons (title, parent,
                                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                                           GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
                                                           nullptr);
  g_free (title);
  gtk_window_set_role (GTK_WINDOW(w), "pan-servers-dialog");
  gtk_window_set_resizable (GTK_WINDOW(w), TRUE);
  g_signal_connect (w, "response", G_CALLBACK(server_list_dialog_response_cb), d);
  g_object_set_data_full (G_OBJECT(w), "dialog", d, delete_server_list_dialog);

  // workarea
  GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
  gtk_container_set_border_width (GTK_CONTAINER(hbox), 12);
  gtk_box_pack_start (GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG(w))), hbox, TRUE, TRUE, 0);


  // create the list
  d->servers_store = gtk_list_store_new (N_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
  w = d->server_tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->servers_store));
  GtkCellRenderer * renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn * column = gtk_tree_view_column_new_with_attributes (_("Servers"), renderer, "text", COL_HOST, nullptr);
  gtk_tree_view_column_set_sort_column_id (column, COL_HOST);
  gtk_tree_view_append_column (GTK_TREE_VIEW (d->server_tree_view), column);
  GtkTreeSelection * selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (d->server_tree_view));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

  // add callbacks
  g_signal_connect (GTK_TREE_VIEW (d->server_tree_view), "row-activated",
                    G_CALLBACK (server_tree_view_row_activated_cb), d->dialog);
  g_signal_connect (G_OBJECT (selection), "changed",
                    G_CALLBACK (server_tree_view_selection_changed_cb), d);

  w = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), d->server_tree_view);
  gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);
  gtk_widget_set_size_request (w, 300, 300);

  // button box
  GtkWidget * bbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 0);

  // add button
  w = gtk_button_new_from_stock (GTK_STOCK_ADD);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(w, _("Add a Server"));
  g_signal_connect (w, "clicked", G_CALLBACK(add_button_clicked_cb), d->dialog);

  // edit button
  w = gtk_button_new_from_stock (GTK_STOCK_EDIT);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(w, _("Edit a Server's Settings"));
  g_signal_connect (w, "clicked", G_CALLBACK(edit_button_clicked_cb), d->dialog);
  d->edit_button = w;

  // remove button
  w = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(w, _("Remove a Server"));
  g_signal_connect (w, "clicked", G_CALLBACK(remove_button_clicked_cb), d);
  d->remove_button = w;

  server_tree_view_refresh (d);
  button_refresh (d);
  return d->dialog;
}


void
render_cert_flag (GtkTreeViewColumn * ,
                         GtkCellRenderer   * renderer,
                         GtkTreeModel      * model,
                         GtkTreeIter       * iter,
                         gpointer            )
{
  int index (0);
  gtk_tree_model_get (model, iter, COL_FLAG, &index, -1);
  g_object_set (renderer, "pixbuf", _icons[index].pixbuf, nullptr);
}


GtkWidget*
sec_dialog_new (Data& data, Queue& queue, Prefs& prefs, GtkWindow* parent)
{
#ifdef HAVE_GNUTLS
  ServerListDialog * d = new ServerListDialog (data, queue, prefs);

  for (guint i=0; i<ICON_QTY; ++i)
    _icons[i].pixbuf = load_icon (_icons[i].pixbuf_file);

  // dialog
  char * title = g_strdup_printf ("Pan: %s", _("SSL Certificates"));
  GtkWidget * w = d->dialog = gtk_dialog_new_with_buttons (title, parent,
                                                           GTK_DIALOG_DESTROY_WITH_PARENT,
                                                           GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
                                                           nullptr);
  g_free (title);
  gtk_window_set_role (GTK_WINDOW(w), "pan-sec-dialog");
  gtk_window_set_resizable (GTK_WINDOW(w), TRUE);
  g_signal_connect (w, "response", G_CALLBACK(server_list_dialog_response_cb), d);
  g_object_set_data_full (G_OBJECT(w), "dialog", d, delete_sec_dialog);

  // workarea
  GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
  gtk_container_set_border_width (GTK_CONTAINER(hbox), 12);
  gtk_box_pack_start (GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG(w))), hbox, TRUE, TRUE, 0);

  // create the list
  d->servers_store = gtk_list_store_new (N_COLUMNS, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
  w = d->server_tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (d->servers_store));

  GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_PIXBUF, "xpad", 2,"ypad", 0,nullptr));
  GtkTreeViewColumn * column = gtk_tree_view_column_new_with_attributes (_("Certificates"), r, nullptr);
  gtk_tree_view_column_set_cell_data_func (column, r, render_cert_flag, nullptr, nullptr);
  gtk_tree_view_append_column (GTK_TREE_VIEW(w), column);

  r = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Servers"), r, "text", COL_HOST, nullptr);
  gtk_tree_view_column_set_sort_column_id (column, COL_HOST);
  gtk_tree_view_append_column (GTK_TREE_VIEW (w), column);
  GtkTreeSelection * selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (w));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

  // add callbacks
  g_signal_connect (GTK_TREE_VIEW (w), "row-activated",
                    G_CALLBACK (server_tree_view_row_activated_cb), d->dialog);
  g_signal_connect (G_OBJECT (selection), "changed",
                    G_CALLBACK (server_tree_view_selection_changed_cb), d);

  w = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), d->server_tree_view);
  gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);
  gtk_widget_set_size_request (w, 300, 300);

  // button box
  GtkWidget * bbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 0);

  // add button
  w = gtk_button_new_from_stock (GTK_STOCK_ADD);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(w, _("Import Certificate"));
  g_signal_connect (w, "clicked", G_CALLBACK(cert_add_button_clicked_cb), d->dialog);

  // inspect button
  w = gtk_button_new_from_stock (GTK_STOCK_FIND);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(w, _("Inspect Certificate"));
  g_signal_connect (w, "clicked", G_CALLBACK(cert_edit_button_clicked_cb), d->dialog);
  d->edit_button = w;

  // remove button
  w = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_widget_set_tooltip_text(w, _("Remove Certificate"));
  g_signal_connect (w, "clicked", G_CALLBACK(cert_remove_button_clicked_cb), d);
  d->remove_button = w;

  sec_tree_view_refresh (d);
  button_refresh (d);
  return d->dialog;
#else
  return 0;
#endif
}

}
