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

#include "prefs.h"

#include <config.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include "group-prefs.h"

namespace pan {

Prefs :: Prefs() :
  _rules_changed(false),
  _rules_enabled(false)
{}

Prefs :: ~Prefs()
{}

// called for open tags <foo bar='baz'>
void Prefs ::start_element(GMarkupParseContext *,
                           gchar const *element_name,
                           gchar const **attribute_names,
                           gchar const **attribute_vals,
                           gpointer prefs_gpointer,
                           GError **)
{
  const std::string s (element_name);
  Prefs& prefs (*static_cast<Prefs*>(prefs_gpointer));

  if (s == "geometry") {
    char const *name(nullptr);
    int x(0), y(0), w(0), h(0);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (!strcmp (*k,"name")) name = *v;
      else if (!strcmp(*k,"x")) x = atoi (*v);
      else if (!strcmp(*k,"y")) y = atoi (*v);
      else if (!strcmp(*k,"w")) w = atoi (*v);
      else if (!strcmp(*k,"h")) h = atoi (*v);
    }
    if (name && *name)
      prefs.set_geometry (name, x, y, w, h);
  }

  if (s == "flag") {
    char const *name(nullptr);
    bool b (false);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (!strcmp (*k,"name")) name = *v;
      else if (!strcmp(*k,"value")) b = *v && **v && tolower(**v)=='t';
    }
    if (name && *name)
      prefs.set_flag (name, b);
  }

  if (s == "string") {
    char const *name(nullptr);
    char const *value(nullptr);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (!strcmp (*k,"name")) name = *v;
      else if (!strcmp(*k,"value")) value = *v;
    }
    if (name && *name && value && *value)
      prefs.set_string (name, value);
  }

  if (s == "int") {
    char const *name(nullptr);
    char const *value(nullptr);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (!strcmp (*k,"name")) name = *v;
      else if (!strcmp(*k,"value")) value = *v;
    }
    if (name && *name && value && *value)
      prefs.set_int (name, atoi(value));
  }

  if (s == "color") {
    char const *name(nullptr);
    char const *value(nullptr);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (!strcmp (*k,"name")) name = *v;
      else if (!strcmp(*k,"value")) value = *v;
    }
    if (name && *name && value && *value)
      prefs.set_color (name, value);
  }
}

void Prefs ::from_string(StringView const &xml)
{
  GMarkupParser p;
  p.start_element = start_element;
  p.end_element = nullptr;
  p.text = nullptr;
  p.passthrough = nullptr;
  p.error = nullptr;
  GMarkupParseContext* c (
    g_markup_parse_context_new (&p, (GMarkupParseFlags)0, this, nullptr));
  GError * gerr (nullptr);
  if (g_markup_parse_context_parse (c, xml.str, xml.len, &gerr)) {
    // FIXME
  }
  g_markup_parse_context_free (c);
}

namespace
{
int const indent_char_len(2);

std::string indent(int depth)
{
  return std::string(depth * indent_char_len, ' '); }

std::string escaped(std::string const &s)
{
  char *pch = g_markup_escape_text(s.c_str(), s.size());
  const std::string ret(pch);
  g_free(pch);
  return ret;
  }

  std::string escaped(bool const b)
  {
    return b ? "true" : "false";
  }

  std::string color_to_string(GdkRGBA const &c)
  {
    gchar *str = gdk_rgba_to_string(&c);
    std::string res(str);
    g_free(str);
    return res;
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

  foreach_const (longs_t, _longs, it)
    out << indent(depth) << "<long name='" << escaped(it->first) << "' value='" << it->second << "'/>\n";

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

bool Prefs ::get_geometry(
  StringView const &window_name, int &x, int &y, int &w, int &h) const
{
  window_to_geometry_t::const_iterator it (_window_to_geometry.find (window_name));
  if (it != _window_to_geometry.end()) {
    Geometry const &g(it->second);
    x = g.x;
    y = g.y;
    w = g.w;
    h = g.h;
  }
  return it != _window_to_geometry.end();
}

void Prefs ::set_geometry(StringView const &key, int x, int y, int w, int h)
{
  if (x>=0 && y>=0) {
    Geometry& g (_window_to_geometry[key]);
    g.x = x;
    g.y = y;
    g.w = w;
    g.h = h;
  }
}

void Prefs ::set_default_geometry(
  StringView const &key, int x, int y, int w, int h)
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
  char const *key(
    (char const *)g_object_get_data(G_OBJECT(widget), PREFS_WIDGET_KEY));

  bool const maximized = gtk_widget_get_window(widget)
                         && (gdk_window_get_state(gtk_widget_get_window(widget))
                             & GDK_WINDOW_STATE_MAXIMIZED);
  if (!maximized)
  {
    int x(0), y(0);
    gtk_window_get_position (GTK_WINDOW(widget), &x, &y);
    static_cast<Prefs*>(pointer)->set_geometry (key, x, y, alloc->width, alloc->height);
  }
}

void Prefs ::set_window(
  StringView const &key, GtkWindow *window, int x, int y, int w, int h)
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

bool Prefs ::get_flag(StringView const &key, bool fallback) const
{
  if (!_flags.count (key))
    _flags[key] = fallback;
  return _flags[key];
}

void Prefs ::set_flag(StringView const &key, bool value)
{
  _flags[key] = value;
  fire_flag_changed (key, value);
}

/***
****  INTS
***/

int Prefs ::get_int(StringView const &key, int fallback) const
{
  if (!_ints.count (key))
    _ints[key] = fallback;
  return _ints[key];
}

int Prefs ::get_int_min(StringView const &key, int fallback) const
{
  if (!_ints.count (key))
    _ints[key] = fallback;
  if (_ints[key] < fallback) _ints[key] = fallback;
  return _ints[key];
}

void Prefs ::set_int(StringView const &key, int value)
{
  _ints[key] = value;
  fire_int_changed (key, value);
}

/***
****  LONG64
***/

uint64_t Prefs ::get_long64(StringView const &key, uint64_t fallback) const
{
  if (!_ints.count (key))
    _longs[key] = fallback;
  return _longs[key];
}

void Prefs ::set_long64(StringView const &key, uint64_t value)
{
  _longs[key] = value;
  fire_long64_changed (key, value);
}


/***
****  STRINGS
***/

StringView Prefs ::get_string(StringView const &key,
                              StringView const &fallback) const
{
  StringView prefs_string;
  if (!_strings.count (key))
    _strings[key] = fallback;
  prefs_string = _strings[key];
  return prefs_string;
}

void Prefs ::set_string(StringView const &key, StringView const &value)
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

void Prefs ::set_color(StringView const &key, GdkRGBA const &value)
{
  _colors[key] = value;
  fire_color_changed (key, value);
}

void Prefs ::set_color(StringView const &key, StringView const &value)
{
  GdkRGBA c;
  if (gdk_rgba_parse (&c, value.to_string().c_str()))
    set_color (key, c);
  else
    Log::add_err_va (_("Couldn't parse %s color \"%s\""), key.to_string().c_str(), value.to_string().c_str());
}

GdkRGBA Prefs ::get_color(StringView const &key,
                          StringView const &fallback_str) const
{
  GdkRGBA fallback;
  g_assert(gdk_rgba_parse (&fallback, fallback_str.to_string().c_str()));
  return get_color(key, fallback);
}

GdkRGBA
Prefs :: get_color (const StringView& key, const GdkRGBA& fallback) const
{
  if (!_colors.count (key))
    _colors[key] = fallback;
  return _colors[key];
}

std::string
Prefs :: get_color_str (const StringView& key, const GdkRGBA& fallback) const
{
  return color_to_string (get_color (key, fallback));
}

std::string
Prefs :: get_color_str (const StringView& key, const StringView& fallback) const
{
  return color_to_string (get_color (key, fallback));
}

/* get string without fallback option */
std::string
Prefs :: get_color_str_wo_fallback (const StringView& key) const
{
  std::string res;
  if (!_colors.count(key)) return "";
  const GdkRGBA& col(_colors[key]);
  return color_to_string (col);
}

}
