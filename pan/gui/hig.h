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

#ifndef _HIG_h_
#define _HIG_h_

#include <gtk/gtk.h>
#include <gdk/gdk.h>

namespace pan
{
  /**
   * Utilities for making a Gnome HIG-compliant dialog.
   *
   * @ingroup GUI
   */
  struct HIG
  {
    static GtkWidget* workarea_create ();

    static void workarea_finish (GtkWidget* workarea, int* row);

    static void workarea_add_section_divider  (GtkWidget   * table,
                                               int         * row);

    static void workarea_add_section_title    (GtkWidget   * table,
                                               int         * row,
                                               const char  * section_title);

    static void workarea_add_section_spacer  (GtkWidget   * table,
                                              int           row,
                                              int           items_in_section);

    static GtkWidget* workarea_add_wide_checkbutton   (GtkWidget   * table,
                                                       int         * row,
                                                       const char  * mnemonic_string,
                                                       bool          is_active);

    static void workarea_add_wide_control   (GtkWidget   * table,
                                             int         * row,
                                             GtkWidget   * control);

    static GtkWidget* workarea_add_label   (GtkWidget   * table,
                                            int           row,
                                            const char  * mnemonic_string);

    static void workarea_add_label   (GtkWidget* table, int  row, GtkWidget* label);
    static void workarea_add_row     (GtkWidget* table, int* row, GtkWidget* label, GtkWidget* control);

    static void workarea_add_control (GtkWidget   * table,
                                      int           row,
                                      GtkWidget   * control);

    static GtkWidget* workarea_add_row  (GtkWidget   * table,
                                         int         * row,
                                         const char  * mnemonic_string,
                                         GtkWidget   * control,
                                         GtkWidget   * mnemonic_or_null_if_control_is_mnemonic= nullptr);

    static void message_dialog_set_text (GtkMessageDialog * dialog,
                                         const char * primary,
                                         const char * secondary);
  };
}

#endif
