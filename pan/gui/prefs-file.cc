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
#include <iostream>
#include <fstream>
#include <string>
extern "C" {
  #include <sys/types.h> // for umask
  #include <sys/stat.h> // for umask
  #include <glib.h>
}
#include "prefs-file.h"

using namespace pan;

void
PrefsFile :: set_from_file (const StringView& filename)
{
  gchar * txt (0);
  gsize len (0);
  if (g_file_get_contents (filename.to_string().c_str(), &txt, &len, 0))
    from_string (StringView(txt,len));
  g_free (txt);
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
  const mode_t old_mask (umask (0177));
  std::ofstream out (_filename.c_str());
  umask (old_mask);
  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
      << "<preferences>\n"
      << s
      << "</preferences>\n";
  out.close ();
}
