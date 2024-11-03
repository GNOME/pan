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
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "hig.h"

#include <iostream>

using namespace pan;

/***
****
***/

GtkWidget*
HIG :: workarea_create (void)
{
  GtkWidget * t = gtk_table_new (4, 100, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE(t), 6);
  gtk_container_set_border_width (GTK_CONTAINER(t), 12);
  return t;
}

void
HIG :: workarea_add_section_divider  (GtkWidget   * table,
                                       int         * row)
{
  GtkWidget * w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 0u, 6u);
  gtk_table_attach (GTK_TABLE(table), w, 0, 4, *row, *row+1, (GtkAttachOptions)0, (GtkAttachOptions)0, 0, 0);
  ++*row;
}

void
HIG :: workarea_add_section_title    (GtkWidget   * table,
                                      int         * row,
                                      const char  * section_title)
{
  char buf[512];
  GtkWidget * l;

  g_snprintf (buf, sizeof(buf), "<b>%s</b>", section_title);
  l = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_label_set_use_markup (GTK_LABEL(l), TRUE);
  gtk_table_attach (GTK_TABLE(table), l, 0, 4, *row, *row+1, (GtkAttachOptions)(GTK_EXPAND|GTK_SHRINK|GTK_FILL), (GtkAttachOptions)0, 0, 0);
  gtk_widget_show (l);
  ++*row;
}

void
HIG :: workarea_add_section_spacer  (GtkWidget   * table,
                                      int           row,
                                      int           items_in_section)
{
  GtkWidget * w;

  // spacer to move the fields a little to the right of the name header
  w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 18u, 0u);
  gtk_table_attach (GTK_TABLE(table), w, 0, 1, row, row+items_in_section, (GtkAttachOptions)0, (GtkAttachOptions)0, 0, 0);
  gtk_widget_show (w);

  // spacer between the controls and their labels
  w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 12u, 0u);
  gtk_table_attach (GTK_TABLE(table), w, 2, 3, row, row+items_in_section, (GtkAttachOptions)0, (GtkAttachOptions)0, 0, 0);
  gtk_widget_show (w);
}

void
HIG :: workarea_add_wide_control   (GtkWidget   * table,
                                    int         * row,
                                    GtkWidget   * control)
{
  gtk_table_attach (GTK_TABLE(table), control, 1, 4, *row, *row+1, (GtkAttachOptions)(GTK_EXPAND|GTK_SHRINK|GTK_FILL), (GtkAttachOptions)0, 0, 0);
  ++*row;
}

GtkWidget *
HIG :: workarea_add_wide_checkbutton   (GtkWidget   * table,
                                        int         * row,
                                        const char  * mnemonic_string,
                                        bool          is_active)
{
  GtkWidget * w (gtk_check_button_new_with_mnemonic (mnemonic_string));
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), is_active);
  workarea_add_wide_control (table, row, w);
  return w;
}

void
HIG :: workarea_add_label   (GtkWidget   * table,
                             int           row,
                             GtkWidget   * label)
{
  if (GTK_IS_LABEL (label)) {
    gtk_label_set_xalign (GTK_LABEL(label), 0.0f);
    gtk_label_set_yalign (GTK_LABEL(label), 0.5f);
  }
  //gtk_table_attach (GTK_TABLE(table), label, 1, 2, row, row+1, GTK_FILL, (GtkAttachOptions)0, 0, 0);
  gtk_table_attach (GTK_TABLE(table), label, 1, 2, row, row+1, GTK_FILL, GTK_FILL, 0, 0);
  gtk_widget_show (label);
}

GtkWidget*
HIG :: workarea_add_label   (GtkWidget   * table,
                             int           row,
                             const char  * mnemonic_string)
{
  GtkWidget * l = gtk_label_new_with_mnemonic (mnemonic_string);
  gtk_label_set_use_markup (GTK_LABEL(l), TRUE);
  workarea_add_label (table, row, l);
  return l;
}


void
HIG :: workarea_add_control (GtkWidget   * table,
                              int           row,
                              GtkWidget   * control)
{
  gtk_table_attach (GTK_TABLE(table), control, 3, 4, row, row+1, (GtkAttachOptions)(GTK_EXPAND|GTK_SHRINK|GTK_FILL), (GtkAttachOptions)0, 0, 0);
}

void
HIG :: workarea_finish (GtkWidget   * table,
                        int         * row)
{
  GtkWidget * w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 0u, 6u);
  gtk_table_attach_defaults (GTK_TABLE(table), w, 0, 4, *row, *row+1);
}

void
HIG :: workarea_add_row (GtkWidget * table,
                         int       * row,
                         GtkWidget * label,
                         GtkWidget * control)
{
  workarea_add_label (table, *row, label);
  workarea_add_control (table, *row, control);
  if (GTK_IS_LABEL(label))
    gtk_label_set_mnemonic_widget (GTK_LABEL(label), control);
  ++*row;
}

GtkWidget*
HIG :: workarea_add_row  (GtkWidget   * table,
                          int         * row,
                          const char  * mnemonic_string,
                          GtkWidget   * control,
                          GtkWidget   * mnemonic_or_null_if_control_is_mnemonic)
{
  GtkWidget * l;
  GtkWidget * mnemonic;

  l = workarea_add_label (table, *row, mnemonic_string);
  workarea_add_control (table, *row, control);

  if (mnemonic_or_null_if_control_is_mnemonic == NULL)
    mnemonic = control;
  else
    mnemonic = mnemonic_or_null_if_control_is_mnemonic;

  gtk_label_set_mnemonic_widget (GTK_LABEL(l), mnemonic);

  ++*row;

  return l;
}

void
HIG :: message_dialog_set_text (GtkMessageDialog * dialog,
                                const char * primary,
                                const char * secondary)
{
  gtk_widget_show_all(GTK_WIDGET(dialog));
  gtk_message_dialog_set_markup (dialog, primary);
  if (secondary) gtk_message_dialog_format_secondary_text (dialog, "%s", secondary);
}
