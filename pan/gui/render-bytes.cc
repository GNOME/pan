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

#include <glib.h> // g_snprintf
#include "render-bytes.h"

namespace pan
{
  char*
  render_bytes (guint64 bytes)
  {
    static const unsigned long KIBI (1024ul);
    static const unsigned long MEBI (1048576ul);
    static const unsigned long GIBI (1073741824ul);
    static char buf[128];

    if (bytes < KIBI)
      g_snprintf (buf, sizeof(buf), "%d B", (int)bytes);
    else if (bytes < MEBI)
      g_snprintf (buf, sizeof(buf), "%.0f KiB", (double)bytes/KIBI);
    else if (bytes < GIBI)
      g_snprintf (buf, sizeof(buf), "%.1f MiB", (double)bytes/MEBI);
    else
      g_snprintf (buf, sizeof(buf), "%.2f GiB", (double)bytes/GIBI);

    return buf;
  }
}
