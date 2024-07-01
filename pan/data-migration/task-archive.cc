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
#include <glib.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/tasks/nzb.h>
#include "data-impl.h"
#include "data-io.h"

void
DataImpl :: save_tasks (const std::vector<Task*>& tasks)
{
  std::ostream * out (_data_io->write_tasks ());
  NZB :: nzb_to_xml (*out, tasks);
  _data_io->write_done (out);
}

void
DataImpl :: load_tasks (std::vector<Task*>& setme)
{
  typedef std::deque<std::string> lines_t;
  lines_t lines;
  StringView line;
  size_t total_len (0);
  LineReader * in (_data_io->read_tasks ());
  while (in && in->getline (line)) {
    total_len += line.len;
    lines.push_back (line.to_string());
  }
  delete in;

  std::string full;
  full.reserve (total_len+1);
  foreach_const (lines_t, lines, it)
    full += *it;

  char * dir (g_get_current_dir ()); // hmm, maybe we could add a tag to nzb for this?
  NZB :: tasks_from_nzb_string (StringView(full), dir, get_cache(), get_encode_cache(), *this, *this, *this, setme);
  g_free (dir);
}
