/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
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

#include "e-charset-combo-box.h"
#include <glib/gi18n.h>
#include "e-charset-dialog.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>

/**
 * e_charset_dialog:
 * @title: title for the dialog box
 * @prompt: prompt string for the dialog box
 * @default_charset: as for e_charset_picker_new()
 * @parent: a parent window for the dialog box, or %NULL
 *
 * This creates a new dialog box with the given @title and @prompt and
 * a character set picker menu. It then runs the dialog and returns
 * the selected character set, or %NULL if the user clicked "Cancel".
 *
 * Return value: the selected character set (which must be freed with
 * g_free()), or %NULL.
 **/
char *
e_charset_dialog (const char *title, const char *prompt,
			 const char *default_charset, GtkWindow *parent)
{
	// Dialog used in Preferences -> Miscellaneous -> Font button
	GtkDialog *dialog;
	GtkWidget *label, *picker, *vbox, *hbox;
	char *charset = NULL;

	dialog = GTK_DIALOG (gtk_dialog_new_with_buttons (title,
							  parent,
							  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
							  _("Cancel"), GTK_RESPONSE_CANCEL,
							  _("OK"), GTK_RESPONSE_OK,
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


	picker = e_charset_combo_box_new ();
        e_charset_combo_box_set_charset (E_CHARSET_COMBO_BOX(picker), default_charset);
	gtk_box_pack_start (GTK_BOX (hbox), picker, TRUE, TRUE, 0);
	gtk_widget_show (picker);

	gtk_container_set_border_width (GTK_CONTAINER (gtk_dialog_get_content_area(dialog)), 0);
	gtk_container_set_border_width (GTK_CONTAINER (gtk_dialog_get_action_area(dialog)), 12);

	gtk_widget_show_all (GTK_WIDGET (dialog));

	g_object_ref (dialog);

	if (gtk_dialog_run (dialog) == GTK_RESPONSE_OK)
		charset = g_strdup (e_charset_combo_box_get_charset (E_CHARSET_COMBO_BOX(picker)));

	gtk_widget_destroy (GTK_WIDGET (dialog));
	g_object_unref (dialog);

	return charset;
}
