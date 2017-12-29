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

#ifndef __Compression_h__
#define __Compression_h__

#include <iostream>
#include <fstream>
#include <config.h>
#include <pan/general/string-view.h>

namespace pan
{

  enum CompressionType
  {
    HEADER_COMPRESS_NONE = 0,
    HEADER_COMPRESS_XZVER = 1,
    HEADER_COMPRESS_XFEATURE = 2,
    HEADER_COMPRESS_DIABLO = 3
  };



  const static char* COMPRESS_GZIP = "[COMPRESS=GZIP]";
  const static char* ENABLE_COMPRESS_GZIP = "XFEATURE COMPRESS GZIP\r\n";

  namespace compression
  {
    bool inflate_zlib(std::stringstream *source, std::stringstream *dest,
        const CompressionType& compression);

    bool ydecode(std::stringstream* in, std::stringstream* out);

    void inflate_gzip (std::stringstream*, std::stringstream*);
  }
}

#endif
