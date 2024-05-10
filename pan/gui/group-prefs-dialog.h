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

#ifndef __GroupPrefsDialog_h__
#define __GroupPrefsDialog_h__

#include <pan/general/quark.h>
#include <pan/data/data.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "group-prefs.h"
#include "prefs.h"

namespace pan
{
  class GroupPrefsDialog
  {
    public:
      GroupPrefsDialog (Data            & data,
                        const quarks_v  & groups,
                        Prefs           & prefs,
                        GroupPrefs      & group_prefs,
                        GtkWindow       * parent_window);

      ~GroupPrefsDialog () {}
      GtkWidget * root() { return _root; }

    private:
      const quarks_v   _groups;
      Prefs        & _prefs;
      GroupPrefs   & _group_prefs;
      GtkWidget    * _root;
      GtkWidget    * _charset;
      GtkWidget    * _profile;
      GtkWidget    * _spellchecker_language;
      GtkWidget    * _group_color;
      GtkWidget    * _save_path;
      GdkRGBA _color;

    private:
      static void response_cb (GtkDialog*, int, gpointer);
      void save_from_gui ();

    public:
      	 GtkWidget* get_color_button() { return _group_color; }
      	 const quarks_v& get_groups() { return _groups; }
      	 GroupPrefs& get_prefs() { return _group_prefs; }
  };
}
#endif
