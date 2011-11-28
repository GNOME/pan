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

#ifndef _Prefs_h_
#define _Prefs_h_

#include <map>
#include <set>
#include <string>
#include <vector>
#include <pan/general/string-view.h>
extern "C"
{
  #include <gtk/gtk.h>
}

namespace pan
{
  /**
   * UI-oriented prefs.
   * @ingroup GUI
   */
  class Prefs
  {
    public:
      struct Listener {
        virtual ~Listener () {}
        virtual void on_prefs_flag_changed (const StringView& key, bool value) = 0;
        virtual void on_prefs_int_changed (const StringView& key, int color) = 0;
        virtual void on_prefs_string_changed (const StringView& key, const StringView& value) = 0;
        virtual void on_prefs_color_changed (const StringView& key, const GdkColor& color) = 0;
      };
      void add_listener (Listener* l) { _listeners.insert(l); }
      void remove_listener (Listener* l) {_listeners.erase(l); }

    private:
      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

    private:
      void fire_flag_changed (const StringView& key, bool value) {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
          (*it++)->on_prefs_flag_changed (key, value);
      }
      void fire_int_changed (const StringView& key, int value) {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
          (*it++)->on_prefs_int_changed (key, value);
      }
      void fire_string_changed (const StringView& key, const StringView& value) {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
          (*it++)->on_prefs_string_changed (key, value);
      }
      void fire_color_changed (const StringView& key, const GdkColor& value) {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
          (*it++)->on_prefs_color_changed (key, value);
      }

    public:
      static void start_element (GMarkupParseContext *context,
                                 const gchar         *element_name,
                                 const gchar        **attribute_names,
                                 const gchar        **attribute_vals,
                                 gpointer             user_data,
                                 GError             **error);

    public:
      bool get_flag (const StringView& key, bool fallback) const;
      void set_flag (const StringView& key, bool);
      int get_int (const StringView& key, int fallback) const;
      int get_int_min  (const StringView& key, int fallback) const;
      void set_int (const StringView& key, int);

      std::string get_string (const StringView& key, const StringView& fallback) const;
      void set_string (const StringView& key, const StringView&);

      void set_color (const StringView& key, const GdkColor& color);
      void set_color (const StringView& key, const StringView& color_str);
      std::string get_color_str (const StringView& key, const GdkColor& fallback) const;
      std::string get_color_str (const StringView& key, const StringView& fallback_str) const;
      std::string get_color_str_wo_fallback (const StringView& key) const;
      GdkColor get_color (const StringView& key, const GdkColor& fallback) const;
      GdkColor get_color (const StringView& key, const StringView& fallback_str) const;

      void set_window (const StringView& key, GtkWindow* window,
                       int default_x, int default_y,
                       int default_width, int default_height);

      void set_default_geometry (const StringView&, int, int, int, int);
      void set_geometry (const StringView&, int, int, int, int);
      bool get_geometry (const StringView&, int&, int&, int&, int&) const;

    public:
      Prefs () { _rules_changed = false; }
      virtual void save () const {}
      virtual ~Prefs () {}

    public:
      void to_string (int indent, std::string& setme) const;
      void from_string (const StringView& xml);

    private:
      struct Geometry {
        int x, y, w, h;
        Geometry(int xx, int yy, int ww, int hh): x(xx), y(yy), w(ww), h(hh) {}
        Geometry(): x(0), y(0), w(0), h(0) {}
      };
      typedef std::map<std::string,Geometry> window_to_geometry_t;
      window_to_geometry_t _window_to_geometry;
      static void window_size_allocated_cb (GtkWidget*, GtkAllocation*, gpointer);

    private:
      typedef std::map<std::string,bool> flags_t;
      mutable flags_t _flags;
      typedef std::map<std::string,std::string> strings_t;
      mutable strings_t _strings;
      typedef std::map<std::string,GdkColor> colors_t;
      mutable colors_t _colors;
      typedef std::map<std::string,int> ints_t;
      mutable ints_t _ints;

    public:
      bool _rules_changed;
      bool _rules_enabled;
  };
}

#endif
