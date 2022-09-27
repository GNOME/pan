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

#ifndef _pan_utf8_utils_h_
#define _pan_utf8_utils_h_

#include <string>
#include <gmime/gmime.h>
#include <pan/general/string-view.h>

namespace pan
{
  std::string header_to_utf8    (const StringView  & in,
                                 const char        * fallback_charset_1=nullptr,
                                 const char        * fallback_charset_2=nullptr);

  std::string mime_part_to_utf8 (GMimePart         * part,
                                 const char        * fallback_charset=nullptr);

  std::string content_to_utf8   (const StringView  & in,
                                 const char        * fallback_charset_1=nullptr,
                                 const char        * fallback_charset_2=nullptr);

  std::string clean_utf8        (const StringView  & in);
}

#endif
