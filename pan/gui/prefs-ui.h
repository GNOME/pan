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

#ifndef PREFS_UI_H
#define PREFS_UI_H

#include <pan/gui/gtk-compat.h>
#include <pan/gui/prefs.h>

namespace pan
{
  class PrefsDialog :
    public Prefs::Listener
  {
    public:
      PrefsDialog (Prefs&, GtkWindow*) ;
      ~PrefsDialog () { }
      Prefs& prefs () { return _prefs; }
      GtkWidget* root() { return _root; }

    private:
      Prefs& _prefs;
      GtkWidget* _root;
      GtkWidget* charset_label;
      void update_default_charset_label(const StringView&);

      void on_prefs_flag_changed (const StringView& key, bool value) {}
      void on_prefs_int_changed (const StringView& key, int color) {}
      void on_prefs_string_changed (const StringView& key, const StringView& value) ;
      void on_prefs_color_changed (const StringView& key, const GdkColor& color) {}

  };
}

#endif
