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

#ifndef PREFS_UI_H
#define PREFS_UI_H

#include <list>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/gui/prefs.h>
#include <pan/gui/group-prefs.h>

namespace pan
{

  class PrefsDialog :
    public Prefs::Listener
  {

    public:

      struct CallBackData
      {
        PrefsDialog* dialog;
        std::string name;
        std::string value;
        GtkWidget* entry;
        GtkWidget* label;
      };

    public:
      PrefsDialog (Prefs&, GtkWindow*) ;
      ~PrefsDialog ();
      Prefs& prefs () { return _prefs; }
      GtkWidget* notebook () { return _notebook; }
      GtkWidget* root() { return _root; }

      static void populate_popup_cb (GtkEntry*, GtkMenu*, gpointer);
      static void edit_shortkey_cb (GtkMenuItem*, gpointer);

      void set_current_hotkey(const char* s) { _hotkey = s; }
      const char* get_current_hotkey() const { return _hotkey; }

    private:
      Prefs& _prefs;
      GtkWidget* _root;
      GtkWidget* charset_label;
      GtkWidget* _notebook;
      std::list<CallBackData*> shortcut_ptr_list;

      void update_default_charset_label(const StringView&);

      void on_prefs_flag_changed (const StringView& key, bool value) override;
      void on_prefs_int_changed (const StringView& key, int color) override {}
      void on_prefs_string_changed (const StringView& key, const StringView& value) override;
      void on_prefs_color_changed (const StringView& key, const GdkRGBA& color) override {}

      const char* _hotkey;

    public:
      void populate_popup (GtkEntry*, GtkMenu*);
      void edit_shortkey (gpointer);
      void update_hotkey (gpointer);


  };
}

#endif
