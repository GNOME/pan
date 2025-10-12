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

extern "C" {
  #include <config.h>
  #include <sys/types.h> // chmod
  #include <sys/stat.h> // chmod
}
#include <glib.h>
#include <iostream>
#include <fstream>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include "group-prefs.h"

using namespace pan;

bool
GroupPrefs ::get_flag (const Quark       & group,
                       const StringView  & key,
                       bool                fallback) const
{
  bool ret (fallback);

  if (_prefs.count (group))
    ret = _prefs[group].get_flag (key, fallback);

  return ret;
}

void
GroupPrefs :: set_flag (const Quark       & group,
                        const StringView  & key,
                        bool                fallback)
{
  _prefs[group].set_flag (key, fallback);
}

int
GroupPrefs :: get_int (const Quark       & group,
                       const StringView  & key,
                       int                 fallback) const
{
  int ret (fallback);

  if (_prefs.count (group))
    ret = _prefs[group].get_int (key, fallback);

  return ret;
}

void
GroupPrefs :: set_int (const Quark       & group,
                       const StringView  & key,
                       int                 fallback)
{
  _prefs[group].set_int (key, fallback);
}

std::string
GroupPrefs :: get_string (const Quark       & group,
                          const StringView  & key,
                          const StringView  & fallback) const
{
  std::string ret (fallback);

  if (_prefs.count (group))
    ret = _prefs[group].get_string (key, fallback);

  return ret;
}

void
GroupPrefs :: set_string (const Quark       & group,
                          const StringView  & key,
                          const StringView  & fallback)
{
  _prefs[group].set_string (key, fallback);
}

/***
****
***/

GroupPrefs :: GroupPrefs ()
{
}

GroupPrefs :: GroupPrefs (const StringView& filename):
  _filename (filename.to_string())
{
  if (!_filename.empty())
    set_from_file (_filename);
}

GroupPrefs :: ~GroupPrefs ()
{
  if (!_filename.empty())
    save ();
}

void
GroupPrefs :: save () const
{
  std::ofstream out (_filename.c_str());
  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n";
  foreach_const (group_prefs_t, _prefs, it) {
    out << "<group name=\"" << it->first << "\">\n";
    std::string s;
    it->second.to_string (2, s);
    out << s;
    out << "</group>\n";
  }
  out.close ();
  chmod (_filename.c_str(), 0600);
}

namespace
{
  Quark group;
}

// called for open tags <foo bar='baz'>
void
GroupPrefs :: start_element (GMarkupParseContext *context,
                             const gchar         *element_name,
                             const gchar        **attribute_names,
                             const gchar        **attribute_vals,
                             gpointer             group_prefs_gpointer,
                             GError             **error)
{
  const std::string s (element_name);
  GroupPrefs& group_prefs (*static_cast<GroupPrefs*>(group_prefs_gpointer));

  if (s == "group")
  {
    const char * name(nullptr);
    for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      if (!strcmp (*k,"name"))  name = *v;
    if (name && *name)
      group = name;
  }
  else if (!group.empty())
  {
    Prefs :: start_element (context,
                            element_name,
                            attribute_names,
                            attribute_vals,
                            &group_prefs._prefs[group],
                            error);
  }
}

namespace
{
  void end_element (GMarkupParseContext *,
                    const gchar         *element_name,
                    gpointer             ,
                    GError             **)
  {
    if (!strcmp (element_name, "group"))
      group.clear ();
  }
}

void
GroupPrefs :: from_string (const StringView& xml)
{
  GMarkupParser p;
  p.start_element = start_element;
  p.end_element = end_element;
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

void
GroupPrefs :: set_from_file (const StringView& filename)
{
  std::string s;
  if (file :: get_text_file_contents (filename, s))
    from_string (s);
}

std::string
GroupPrefs :: get_group_color_str (const Quark& group) const
{
  if (!_prefs[group].get_colors().count("group-color"))
    return "";
  const GdkRGBA& col (_prefs[group].get_colors()["group-color"]);
  return gdk_rgba_to_string (&col);
}

GdkRGBA
GroupPrefs :: get_group_color (const Quark& group, const StringView& fallback_str) const
{
  GdkRGBA fallback;
  gdk_rgba_parse (&fallback, fallback_str.to_string().c_str());
  return _prefs[group].get_color("group-color", fallback);
}

void
GroupPrefs :: set_group_color (const Quark& group, const GdkRGBA& color)
{
	_prefs[group].set_color("group-color", color);
}
