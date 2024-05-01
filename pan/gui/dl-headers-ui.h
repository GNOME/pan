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
#ifndef DOWNLOAD_HEADERS_UI_H
#define DOWNLOAD_HEADERS_UI_H

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/quark.h>
#include "prefs.h"

namespace pan
{
  struct Data;
  struct Queue;
  void headers_dialog (Data&, Prefs&, Queue&, const quarks_t& groups, GtkWindow* parent);
}

#endif
