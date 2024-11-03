/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/*
 * Copyright (C) 2001 Ximian, Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "e-cte-dialog.h"

/**
 * e_cte_dialog:
 * @title: title for the dialog box
 * @prompt: prompt string for the dialog box
 * @now: active cte
 * @parent: a parent window for the dialog box, or %NULL
 *
 * This creates a new dialog box with the given @title and @prompt and
 * a cte set picker menu. It then runs the dialog and returns
 * the selected content-transfer-encoding, or GMIME_CONTENT_ENCODING_8BIT if the user cancels/aborts.
 *
 * Return value: the selected content transfer encoding, or GMIME_CONTENT_ENCODING_8BIT.
 **/
GMimeContentEncoding
e_cte_dialog (const char *title, const char *prompt, GMimeContentEncoding now, GtkWindow *parent)
{
	GtkDialog *dialog;
	GtkWidget *label, *picker, *vbox, *hbox;
  GMimeContentEncoding ret = GMIME_CONTENT_ENCODING_8BIT;

	dialog = GTK_DIALOG (gtk_dialog_new_with_buttons (title,
                       parent,
                       GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                       GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                       GTK_STOCK_OK, GTK_RESPONSE_OK,
                       NULL));

	//gtk_dialog_set_has_separator (dialog, FALSE);
	gtk_dialog_set_default_response (dialog, GTK_RESPONSE_OK);

	vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_container_set_border_width (GTK_CONTAINER (vbox), 12);
	gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area(dialog)), vbox, FALSE, FALSE, 0);
	gtk_widget_show (vbox);

	label = gtk_label_new (prompt);
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_label_set_xalign (GTK_LABEL (label), 0.0);
	gtk_label_set_yalign (GTK_LABEL (label), 0.5);
	gtk_box_pack_start (GTK_BOX (vbox), label, FALSE, FALSE, 0);
	gtk_widget_show (label);

	hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 12);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);
	gtk_widget_show (hbox);

	label = gtk_label_new ("");
	gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
	gtk_widget_show (label);

  GtkTreeIter iter;
  GtkListStore * store;
  GtkCellRenderer * renderer;

  store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_INT);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Default Encoding"),           1, GMIME_CONTENT_ENCODING_DEFAULT, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("7-Bit Encoding"),              1, GMIME_CONTENT_ENCODING_7BIT, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("8-Bit Encoding"),              1, GMIME_CONTENT_ENCODING_8BIT, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Base64 Encoding"),            1, GMIME_CONTENT_ENCODING_BASE64, -1);
  gtk_list_store_append (store, &iter);
  gtk_list_store_set (store, &iter, 0, _("Quoted-Printable Encoding"),  1, GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE, -1);

  int active = 0;
  switch (now)
  {
    case GMIME_CONTENT_ENCODING_DEFAULT:
      active = 0;
      break;
    case GMIME_CONTENT_ENCODING_7BIT:
      active = 1;
      break;
    case GMIME_CONTENT_ENCODING_8BIT:
      active = 2;
      break;
    case GMIME_CONTENT_ENCODING_BASE64:
      active = 3;
      break;
    case GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE:
      active = 4;
      break;
  }
  picker = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
  gtk_combo_box_set_active (GTK_COMBO_BOX(picker), active);

  renderer = gtk_cell_renderer_text_new ();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (picker), renderer, TRUE);
  gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (picker), renderer, "text", 0, NULL);

	gtk_box_pack_start (GTK_BOX (hbox), picker, TRUE, TRUE, 0);

	gtk_container_set_border_width (GTK_CONTAINER (gtk_dialog_get_content_area(dialog)), 0);
	gtk_container_set_border_width (GTK_CONTAINER (gtk_dialog_get_action_area(dialog)), 12);

	gtk_widget_show_all (GTK_WIDGET (dialog));
	gtk_widget_show_all (GTK_WIDGET (hbox));

	g_object_ref (dialog);

	if (gtk_dialog_run (dialog) == GTK_RESPONSE_OK)
	{
    GtkTreeModel *model;
    GtkTreeIter iter;
    GMimeContentEncoding value;

    if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX(picker), &iter))
    {
      model = gtk_combo_box_get_model (GTK_COMBO_BOX(picker));
      gtk_tree_model_get (model, &iter, 1, &value, -1);
      ret = value;
    }
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_object_unref (dialog);

	return ret;
}
