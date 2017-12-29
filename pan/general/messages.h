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

#ifndef __Messages_h__
#define __Messages_h__

#include <iostream>

#define pan_return_if_fail(expr) \
  do { \
    if (!(expr)) { \
      std::cerr << "(" << __FILE__ << ':' << __LINE__ << " " << __func__ << "): assertion \"" << #expr << "\" failed.\n"; \
      return; \
    } \
  } while (0)

#define pan_return_val_if_fail(expr, val) \
  do { \
    if (!(expr)) { \
      std::cerr << "(" << __FILE__ << ':' << __LINE__ << " " << __func__ << "): assertion \"" << #expr << "\" failed.\n"; \
      return val; \
    } \
  } while (0)

#endif
