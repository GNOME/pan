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

#ifndef __Debug_h__
#define __Debug_h__

#include <iostream>
#include <fstream>

namespace pan
{
  extern bool _debug_flag;
  extern bool _debug_verbose_flag;
  extern bool _verbose_flag;
  extern bool _dbg_ssl;
  extern std::ofstream _dbg_file;

}

#define LINE_ID '(' << __FILE__ << ':' << __LINE__ << ':' << __func__ << ')'

#define pan_debug(A) \
  do { \
    if (_debug_flag) \
      std::cerr<< LINE_ID << ' ' << A << '\n'; \
  } while (0)

#define pan_debug_v(A) \
  do { \
    if (_debug_verbose_flag) \
      std::cerr << LINE_ID << ' ' << A << '\n'; \
  } while (0)

#define verbose(A) \
  do { \
    if (_verbose_flag) \
      std::cerr << A << '\n'; \
  } while (0)

#define debug_SSL(A) \
  do { \
    if (_dbg_ssl) \
     std::cerr << A << '\n'; \
  } while (0)

#define debug_SSL_verbatim(A) \
  do { \
    if (_dbg_ssl) \
      std::cerr << A << '\n'; \
  } while (0)

#ifndef UNUSED
#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif
#endif


#endif
