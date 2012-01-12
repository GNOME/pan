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
extern "C" {
  #include <glib/gi18n.h>
}

#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/log.h>

#include "data-io.h"
#include "data-impl.h"

using namespace pan;

int
DataImpl :: load_hotkeys(const DataIO& io)
{
  LineReader * in (io.read_hotkeys());

  StringView line, tok;
  StringView tokens[2]; // tokens[0] is the action string, tokens[1] is the hotkey string
  int i(0);
  int num_keys(0), num_togglekeys(0);
  bool togglekeys(false);

  while (in && !in->fail() && in->getline(line))
  {
    if (line.len && *line.str=='#')
    {
      if (line.strstr("toggle")) togglekeys=true;
      continue;
    }

    tokens[0].clear();
    tokens[1].clear();

    while (line.pop_token(tok))
    {
      tokens[i++] = tok;
      if (i>1) break;
    }
    i = 0;

    if (!togglekeys)
    {
      _hotkeys[tokens[0]] = tokens[1];
      ++num_keys;
    }
    else
    {
      _toggle_hotkeys[tokens[0]] = tokens[1];
      ++num_togglekeys;
    }
  }

  const int cnt (num_keys+num_togglekeys);

  if (cnt > 0) Log::add_info_va ("Imported %d shortcuts from file.",cnt);

  delete in;

  return cnt;
}

void
DataImpl :: save_hotkeys (DataIO& io) const
{

  if (_unit_test)
    return;

  if (_hotkeys.empty() && _toggle_hotkeys.empty()) return;

  std::ostream& out (*io.write_hotkeys ());

  out << "# Hotkeys file. Hotkeys are separated with spaces.\n"
      << "# Each hotkey occupies one line.\n"
      << "# Please don't edit this file, Pan handles that!\n"
      << "# If you want to edit it anyway for testing, please \n"
      << "# leave the comment lines intact!!\n"
      << "# \n"
      << "# normal hotkeys\n"
      << "# \n";

  foreach_const (hotkeys_t, _hotkeys, it) {
    out << it->first;
    out.put (' ');
    out << it->second;
    out.put ('\n');
  }

  out<<"#\n# toggle hotkeys\n#\n";

  foreach_const (hotkeys_t, _toggle_hotkeys, it) {
    out << it->first;
    out.put (' ');
    out << it->second;
    out.put ('\n');
  }

  io.write_done (&out);

}

