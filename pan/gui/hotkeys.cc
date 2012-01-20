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
  #include <gtk/gtk.h>
}

#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/log.h>

#include <pan/data-impl/data-io.h>
#include <pan/data-impl/data-impl.h>

#include "hotkeys.h"

using namespace pan;

namespace
{
  void foreach_fill_func          (gpointer data,
                                   const gchar *accel_path,
                                   guint accel_key,
                                   GdkModifierType accel_mods,
                                   gboolean changed)
  {
    std::map<gchar*,guint>* map = static_cast<std::map<gchar*,guint>*>(data);

  }
}

int
fill_hotkeys(std::map<gchar*, guint>& keymap)
{
  gtk_accel_map_foreach (&keymap, foreach_fill_func);
  return 0;
}

