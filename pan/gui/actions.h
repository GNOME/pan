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
#ifndef _actions_h_
#define _actions_h_

#include "gtk-compat.h"
#include <pan/gui/pan-ui.h>
#include <pan/gui/prefs.h>
#include <pan/data/data.h>

namespace pan
{
  void add_actions (PanUI*, GtkUIManager*, Prefs*, Data*);
  void add_hotkeys_to_actions (hotkeys_t& hk, hotkeys_t& t_hk);
  void add_new_hotkeys_from_ui (hotkeys_t& hk, hotkeys_t& t_hk);
}

#endif /* __PAN_H__ */
