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
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
extern "C" {
  #include <glib.h>
  #include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include "prefs.h"

using namespace pan;

// called for open tags <foo bar='baz'>
void
Prefs :: start_element (GMarkupParseContext *,
                        const gchar         *element_name,
                        const gchar        **attribute_names,
                        const gchar        **attribute_vals,
                        gpointer             prefs_gpointer,
                        GError             **)
{
  const std::string s (element_name);
  Prefs& prefs (*static_cast<Prefs*>(prefs_gpointer));

  if (s == "geometry") {
    const char * name(0);
    int x(0), y(0), w(0), h(0);
    for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      if (!strcmp (*k,"name"))  name = *v;
      else if (!strcmp(*k,"x")) x = atoi (*v);
      else if (!strcmp(*k,"y")) y = atoi (*v);
      else if (!strcmp(*k,"w")) w = atoi (*v);
      else if (!strcmp(*k,"h")) h = atoi (*v);
    if (name && *name)
      prefs.set_geometry (name, x, y, w, h);
  }

  if (s == "flag") {
    const char * name (0);
    bool b (false);
    for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      if (!strcmp (*k,"name"))  name = *v;
      else if (!strcmp(*k,"value")) b = *v && **v && tolower(**v)=='t';
    if (name && *name)
      prefs.set_flag (name, b);
  }

  if (s == "string") {
    const char * name (0);
    const char * value (0);
    for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      if (!strcmp (*k,"name"))  name = *v;
      else if (!strcmp(*k,"value")) value = *v;
    if (name && *name && value && *value)
      prefs.set_string (name, value);
  }

  if (s == "int") {
    const char * name (0);
    const char * value (0);
    for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      if (!strcmp (*k,"name"))  name = *v;
      else if (!strcmp(*k,"value")) value = *v;
    if (name && *name && value && *value)
      prefs.set_int (name, atoi(value));
  }

  if (s == "color") {
    const char * name (0);
    const char * value (0);
    for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      if (!strcmp (*k,"name"))  name = *v;
      else if (!strcmp(*k,"value")) value = *v;
    if (name && *name && value && *value)
      prefs.set_color (name, value);
  }
}

void
Prefs :: from_string (const StringView& xml)
{
  GMarkupParser p;
  p.start_element = start_element;
  p.end_element = 0;
  p.text = 0;
  p.passthrough = 0;
  p.error = 0;
  GMarkupParseContext* c (
    g_markup_parse_context_new (&p, (GMarkupParseFlags)0, this, 0));
  GError * gerr (0);
  if (g_markup_parse_context_parse (c, xml.str, xml.len, &gerr)) {
    // FIXME
  }
  g_markup_parse_context_free (c);
}

namespace
{
  const int indent_char_len (2);

  std::string indent (int depth) { return std::string(depth*indent_char_len, ' '); }

  std::string escaped (const std::string& s)
  {
    char * pch = g_markup_escape_text (s.c_str(), s.size());
    const std::string ret (pch);
    g_free (pch);
    return ret;
  }

  std::string escaped (const bool b)
  {
    return b ? "true" : "false";
  }

  std::string color_to_string (const GdkColor& c)
  {
    char buf[8];
    g_snprintf (buf, sizeof(buf), "#%02x%02x%02x", c.red/0x100, c.green/0x100, c.blue/0x100);
    return buf;
  }
}

void
Prefs :: to_string (int depth, std::string& setme) const
{
  std::ostringstream out;

  foreach_const (flags_t, _flags, it)
    out << indent(depth) << "<flag name='" << escaped(it->first) << "' value='" << (it->second ? "true" : "false") << "'/>\n";

  foreach_const (ints_t, _ints, it)
    out << indent(depth) << "<int name='" << escaped(it->first) << "' value='" << it->second << "'/>\n";

  foreach_const (strings_t, _strings, it)
    out << indent(depth) << "<string name='" << escaped(it->first) << "' value='" << escaped(it->second) << "'/>\n";

  foreach_const (colors_t, _colors, it)
    out << indent(depth) << "<color name='" << escaped(it->first) << "' value='" << color_to_string(it->second) << "'/>\n";

  foreach_const (window_to_geometry_t, _window_to_geometry, it) {
    if (it->second.x || it->second.y || it->second.w || it->second.h) {
      out << indent(depth) << "<geometry name='" << it->first << "'";
      if (it->second.x) out << " x='" << it->second.x << "'";
      if (it->second.y) out << " y='" << it->second.y << "'";
      if (it->second.w) out << " w='" << it->second.w << "'";
      if (it->second.h) out << " h='" << it->second.h << "'";
      out << "/>\n";
    }
  }

  setme = out.str();
}

/***
****  WINDOW GEOMETRY
***/

bool
Prefs :: get_geometry (const StringView& window_name, int& x, int& y, int& w, int& h) const
{
  window_to_geometry_t::const_iterator it (_window_to_geometry.find (window_name));
  if (it != _window_to_geometry.end()) {
    const Geometry& g (it->second);
    x = g.x;
    y = g.y;
    w = g.w;
    h = g.h;
  }
  return it != _window_to_geometry.end();
}

void
Prefs :: set_geometry (const StringView& key, int x, int y, int w, int h)
{
  if (x>=0 && y>=0) {
    Geometry& g (_window_to_geometry[key]);
    g.x = x;
    g.y = y;
    g.w = w;
    g.h = h;
  }
}

void
Prefs :: set_default_geometry (const StringView& key, int x, int y, int w, int h)
{
  if (!_window_to_geometry.count (key))
    set_geometry (key, x, y, w, h);
}

#define PREFS_WIDGET_KEY "prefs-widget-key"

void
Prefs :: window_size_allocated_cb (GtkWidget      * widget,
                                   GtkAllocation  * alloc,
                                   gpointer         pointer)
{
  const char * key ((const char*) g_object_get_data (G_OBJECT(widget), PREFS_WIDGET_KEY));

  const bool maximized = GTK_WIDGET(widget)->window
                      && (gdk_window_get_state(widget->window) & GDK_WINDOW_STATE_MAXIMIZED);
  if (!maximized)
  {
    int x(0), y(0);
    gtk_window_get_position (GTK_WINDOW(widget), &x, &y);
    static_cast<Prefs*>(pointer)->set_geometry (key, x, y, alloc->width, alloc->height);
  }
}

void
Prefs :: set_window (const StringView& key, GtkWindow* window, int x, int y, int w, int h)
{
  get_geometry (key, x, y, w, h);
  gtk_window_move (window, x, y);
  gtk_window_set_default_size (window, w, h);
  g_object_set_data_full (G_OBJECT(window), PREFS_WIDGET_KEY, g_strdup(key.to_string().c_str()), g_free);
  g_signal_connect (window, "size-allocate", G_CALLBACK(window_size_allocated_cb), this);
}

/***
****  FLAGS
***/

bool
Prefs :: get_flag (const StringView& key, bool fallback) const
{
  if (!_flags.count (key))
    _flags[key] = fallback;
  return _flags[key];
}

void
Prefs :: set_flag (const StringView& key, bool value)
{
  _flags[key] = value;
  fire_flag_changed (key, value);
}

/***
****  INTS
***/

int
Prefs :: get_int (const StringView& key, int fallback) const
{
  if (!_ints.count (key))
    _ints[key] = fallback;
  return _ints[key];
}

void
Prefs :: set_int (const StringView& key, int value)
{
  _ints[key] = value;
  fire_int_changed (key, value);
}

/***
****  STRINGS
***/

std::string
Prefs :: get_string (const StringView& key, const StringView& fallback) const
{
  if (!_strings.count (key))
    _strings[key] = fallback;
  return _strings[key];
}

void
Prefs :: set_string (const StringView& key, const StringView& value)
{
  std::string& lvalue = _strings[key];
  const std::string old (lvalue);
  lvalue.assign (value.str, value.len);
  if (old != lvalue)
    fire_string_changed (key, value);
}

/***
****  COLOR
***/

void
Prefs :: set_color (const StringView& key, const GdkColor& value)
{
  _colors[key] = value;
  fire_color_changed (key, value);
}

void
Prefs :: set_color (const StringView& key, const StringView& value)
{
  GdkColor c;
  if (gdk_color_parse (value.to_string().c_str(), &c))
    set_color (key, c);
  else
    Log::add_err_va (_("Couldn't parse %s color \"%s\""), key.to_string().c_str(), value.to_string().c_str());
}

GdkColor
Prefs :: get_color (const StringView& key, const StringView& fallback_str) const
{
  GdkColor fallback;
  gdk_color_parse (fallback_str.to_string().c_str(), &fallback);
  return get_color (key, fallback);
}

GdkColor
Prefs :: get_color (const StringView& key, const GdkColor& fallback) const
{
  if (!_colors.count (key))
    _colors[key] = fallback;
  return _colors[key];
}

std::string
Prefs :: get_color_str (const StringView& key, const GdkColor& fallback) const
{
  return color_to_string (get_color (key, fallback));
}

std::string
Prefs :: get_color_str (const StringView& key, const StringView& fallback) const
{
  return color_to_string (get_color (key, fallback));
}
