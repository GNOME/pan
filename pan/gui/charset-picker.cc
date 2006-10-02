/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002  Charles Kerr <charles@rebelbase.com>
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
  #include <string.h>
  #include <locale.h>
  #include <glib.h>
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/usenet-utils/utf8-utils.h>
#include "charset-picker.h"

#define PAN_DEFAULT_CHARSET "UTF-8"

namespace
{
  struct CharsetStruct {
    const char *charset, *name;
  } charsets[] = {
    {"ISO-8859-4",   N_("Baltic")},
    {"ISO-8859-13",  N_("Baltic")},
    {"windows-1257", N_("Baltic")},
    {"ISO-8859-2",   N_("Central European")},
    {"windows-1250", N_("Central European")},
    {"gb2312",       N_("Chinese Simplified")},
    {"big5",         N_("Chinese Traditional")},
    {"ISO-8859-5",   N_("Cyrillic")},
    {"windows-1251", N_("Cyrillic")}, 
    {"KOI8-R",       N_("Cyrillic")},
    {"KOI8-U",       N_("Cyrillic, Ukrainian")},
    {"ISO-8859-7",   N_("Greek")},
    {"ISO-2022-jp",  N_("Japanese")},
    {"euc-kr",       N_("Korean")},
    {"ISO-8859-9",   N_("Turkish")},
    {"ISO-8859-1",   N_("Western")},
    {"ISO-8859-15",  N_("Western, New")},
    {"windows-1252", N_("Western")},
    {"UTF-8",        N_("Unicode")}
  };
}

namespace pan
{
  GtkWidget *
  charset_picker_new (const char *charset)
  {
    int sel_index (0);
    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
    for (int i(0), n(G_N_ELEMENTS(charsets)); i<n; ++i)
    {
      char buf[512];
      g_snprintf (buf, sizeof(buf), "%s (%s)", _(charsets[i].name), charsets[i].charset);

      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, charsets[i].charset, 1, buf, -1);

      if (charset && !strcmp (charsets[i].charset, charset))
        sel_index = i;
    }

    GtkWidget * w = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 1, NULL);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), sel_index);

    g_object_unref (G_OBJECT(store));
    return w;
  }

  char *
  charset_picker_free (GtkWidget * w)
  {
    return NULL;
  }

  bool
  charset_picker_set_charset (GtkWidget * w, const char * charset)
  {
    GtkComboBox * combo (GTK_COMBO_BOX(w));
    GtkTreeModel * model (gtk_combo_box_get_model (combo));
    bool ret (false);

    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first (model, &iter)) do {
      char * row_charset;
      gtk_tree_model_get (model, &iter, 0, &row_charset, -1);
      const bool match (charset && !strcmp (row_charset, charset));
      g_free (row_charset);
      if (match) {
        ret = true;
        gtk_combo_box_set_active_iter (combo, &iter);
        break;
      }
    } while (gtk_tree_model_iter_next(model, &iter));

    return ret;
  }

  std::string
  charset_picker_get_charset (GtkWidget * w)
  {
    std::string ret;
    char * str;
    GtkTreeIter iter;
    GtkComboBox * combo (GTK_COMBO_BOX (w));
    gtk_combo_box_get_active_iter (combo, &iter);
    GtkTreeModel * model (gtk_combo_box_get_model (combo));
    gtk_tree_model_get (model, &iter, 0, &str, -1);
    if (str && *str)
      ret = str;
    g_free (str);
    return ret;
  }

  bool
  charset_picker_has (const char * charset)
  {
    for (size_t i(0), n(G_N_ELEMENTS(charsets)); i!=n; ++i)
      if (charset && !strcmp (charset, charsets[i].charset))
        return true;

    return false;
  }
}
