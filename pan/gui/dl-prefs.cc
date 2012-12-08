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
extern "C" {
  #include <glib/gi18n.h>
  #include "gtk-compat.h"
}
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/icons/pan-pixbufs.h>
#include "hig.h"
#include "pad.h"
#include "pan-file-entry.h"
#include "dl-prefs.h"
#include "url.h"
#include "gtk-compat.h"

using namespace pan;

namespace pan
{

  namespace
  {
    #define PREFS_KEY "prefs-key"
    #define PREFS_VAL "prefs-val"

    void toggled_cb (GtkToggleButton * toggle, gpointer prefs_gpointer)
    {
      const char * key = (const char*) g_object_get_data (G_OBJECT(toggle), PREFS_KEY);
      if (key)
        static_cast<Prefs*>(prefs_gpointer)->set_flag (key, gtk_toggle_button_get_active(toggle));
    }

    GtkWidget* new_check_button (const char* mnemonic, const char* key, bool fallback, Prefs& prefs)
    {
      GtkWidget * t = gtk_check_button_new_with_mnemonic (mnemonic);
      g_object_set_data_full (G_OBJECT(t), PREFS_KEY, g_strdup(key), g_free);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(t), prefs.get_flag (key, fallback));
      g_signal_connect (t, "toggled", G_CALLBACK(toggled_cb), &prefs);
      return t;
    }

    void response_cb (GtkDialog * dialog, int, gpointer)
    {
      gtk_widget_destroy (GTK_WIDGET(dialog));
    }

    void delete_dialog (gpointer castme)
    {
      DLMeterDialog* meterdialog(static_cast<DLMeterDialog*>(castme));
      meterdialog->meter().dl_meter_update();
      delete meterdialog;
    }

    void spin_value_changed_cb( GtkSpinButton *spin, gpointer data)
    {
      const char * key = (const char*) g_object_get_data (G_OBJECT(spin), PREFS_KEY);
      Prefs *prefs = static_cast<Prefs*>(data);
      prefs->set_int(key, gtk_spin_button_get_value_as_int(spin));
    }

    GtkWidget* new_spin_button (const char *key, int low, int high, Prefs &prefs)
    {
      guint tm = prefs.get_int(key, 5 );
      GtkAdjustment *adj = (GtkAdjustment*) gtk_adjustment_new(tm, low, high, 1.0, 1.0, 0.0);
      GtkWidget *w = gtk_spin_button_new( adj, 1.0, 0);
      g_object_set_data_full(G_OBJECT(w), PREFS_KEY, g_strdup(key), g_free);
      g_signal_connect (w, "value_changed", G_CALLBACK(spin_value_changed_cb), &prefs);
      return w;
    }

    void set_prefs_string_from_combobox (GtkComboBox * c, gpointer user_data)
    {
      Prefs * prefs (static_cast<Prefs*>(user_data));
      const char * key = (const char*) g_object_get_data (G_OBJECT(c), PREFS_KEY);

      const int column = GPOINTER_TO_INT (g_object_get_data (G_OBJECT(c), "column"));
      const int row (gtk_combo_box_get_active (c));
      GtkTreeModel * m = gtk_combo_box_get_model (c);
      GtkTreeIter i;
      if (gtk_tree_model_iter_nth_child (m, &i, 0, row)) {
        char * val (0);
        gtk_tree_model_get (m, &i, column, &val, -1);
        prefs->set_string (key, val);
        g_free (val);
      }
    }

    GtkWidget* new_bytes_combo_box (Prefs& prefs,
                                    const char * mode_key)
      {

        const char* strings[2][2] =
        {
          {N_("MB"), "mb"},
          {N_("GB"), "gb"}
        };

        const std::string mode (prefs.get_string (mode_key, "mb"));
        GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

        int sel_index (0);
        for (size_t i=0; i<G_N_ELEMENTS(strings); ++i) {
          GtkTreeIter iter;
          gtk_list_store_append (store, &iter);
          gtk_list_store_set (store, &iter, 0, strings[i][0], 1, strings[i][1], -1);
          if (mode == strings[i][1])
            sel_index = i;
        }

        GtkWidget * c = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
        GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
        gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (c), renderer, true);
        gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (c), renderer, "text", 0, NULL);
        gtk_combo_box_set_active (GTK_COMBO_BOX(c), sel_index);
        g_object_set_data_full (G_OBJECT(c), PREFS_KEY, g_strdup(mode_key), g_free);
        g_object_set_data (G_OBJECT(c), "column", GINT_TO_POINTER(1));
        g_signal_connect (c, "changed", G_CALLBACK(set_prefs_string_from_combobox), &prefs);

        gtk_widget_show_all(c);

        return c;
      }

      void reset_dl_limit_cb (GtkButton *, gpointer user_data)
      {
        DLMeterDialog* pd (static_cast<DLMeterDialog*>(user_data));
        pd->prefs().set_int ("dl-limit", 0);
        pd->meter().dl_meter_reset ();
      }

  }

  DLMeterDialog :: DLMeterDialog (Prefs& prefs, DownloadMeter& meter, GtkWindow* parent):
    _prefs (prefs), _meter(meter)
  {

    GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Download Meter Preferences"), parent,
                                                      GTK_DIALOG_DESTROY_WITH_PARENT,
                                                      GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                                      NULL);
    gtk_window_set_role (GTK_WINDOW(dialog), "pan-dl-preferences-dialog");
    g_signal_connect (dialog, "response", G_CALLBACK(response_cb), this);
    g_signal_connect_swapped (dialog, "destroy", G_CALLBACK(delete_dialog), this);

    // Behavior
    int row (0);
    GtkWidget *h, *w, *l, *b, *t;
    t = HIG :: workarea_create ();
    HIG::workarea_add_section_title (t, &row, _("When Download Limit Is Reached:"));

      HIG :: workarea_add_section_spacer (t, row, 2);
      w = new_check_button (_("Warn"), "warn-dl-limit-reached", false, prefs);
      HIG :: workarea_add_wide_control (t, &row, w);
      w = new_check_button (_("Disconnect from server"), "disconnect-on-dl-limit-reached", false, prefs);
      HIG :: workarea_add_wide_control (t, &row, w);
      HIG::workarea_add_section_title (t, &row, _("Download Limit"));
      w = _spin = new_spin_button ("dl-limit", 1, 1024, prefs);
      HIG :: workarea_add_wide_control (t, &row, w);
      w = new_bytes_combo_box(prefs, "dl-limit-type");
      HIG :: workarea_add_wide_control (t, &row, w);
      w = gtk_button_new_with_label (_("Reset"));
      g_signal_connect (w, "clicked", G_CALLBACK(reset_dl_limit_cb), this);
      HIG :: workarea_add_wide_control (t, &row, w);

    HIG :: workarea_finish (t, &row);

    gtk_widget_show_all(t);

    gtk_box_pack_start (GTK_BOX(gtk_dialog_get_content_area( GTK_DIALOG(dialog))), t, true, true, 0);

    _root = dialog;

  }

}
