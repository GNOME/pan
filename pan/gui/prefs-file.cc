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
#include <iostream>
#include <fstream>
#include <string>
extern "C" {
  #include <sys/types.h> // for chmod
  #include <sys/stat.h> // for chmod
}
#include <glib.h>
#include <pan/general/file-util.h>
#include "prefs-file.h"

using namespace pan;

void
PrefsFile :: set_from_file (const StringView& filename)
{
  std::string s;
  if (file :: get_text_file_contents (filename, s))
    from_string (s);
}


PrefsFile :: PrefsFile (const StringView& filename):
  _filename (filename.to_string())
{
  set_from_file (_filename);
}

PrefsFile :: ~PrefsFile ()
{
  save ();
}

void
PrefsFile :: save () const
{
  std::string s;
  to_string (0, s);
  std::ofstream out (_filename.c_str());
  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
      << "<preferences>\n"
      << s
      << "</preferences>\n";
  out.close ();
  chmod (_filename.c_str(), 0600);
}
