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

#ifndef __Pan_Macros_h__
#define __Pan_Macros_h__

#define STD_SSL_PORT 563
#define STD_NNTP_PORT 119

/**
***  foreach
**/

#define foreach(Type,var,itname) \
  for (Type::iterator itname(var.begin()), \
                            itname##end(var.end()); itname!=itname##end; \
                            ++itname)

#define foreach_const(Type,var,itname) \
  for (Type::const_iterator itname(var.begin()), \
                            itname##end(var.end()); itname!=itname##end; \
                            ++itname)

#define foreach_r(Type,var,itname) \
  for (Type::reverse_iterator itname(var.rbegin()), \
                              itname##end(var.rend()); itname!=itname##end; \
                              ++itname)

#define foreach_const_r(Type,var,itname) \
  for (Type::const_reverse_iterator itname(var.rbegin()), \
                           itname##end(var.rend()); itname!=itname##end; \
                           ++itname)

/**
***  UNUSED
**/

#ifndef UNUSED
#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif
#endif

/**
***  g_assert
**/

#include <glib.h>

#endif
