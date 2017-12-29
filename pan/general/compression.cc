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

extern "C"
{
  #include <zlib.h>
  #include <assert.h>
  #include <stdlib.h>
}

#include <sstream>
#include <vector>

#include "compression.h"
#include <uulib/crc32.h>
#include <zlib.h>

using namespace pan;

namespace
{

  #define MEMCHUNK 4096
  #define LINELEN_MIN     2048
  #define LINELEN_MAX     32768

}

bool
compression::ydecode(std::stringstream* in, std::stringstream* out)
{
  int gotbeg = 0, len, outlen = 0;
  char buf2[512];
  char buf1[512], c, *p, *p2 = buf2;
  unsigned int crc1 = 0;
  crc32_t crc = crc32(0L, Z_NULL, 0);

  while (!in->getline(buf1, sizeof(buf1)).bad())
    {
      if (gotbeg == 0 && strncmp(buf1, "=ybegin ", 8) == 0)
        {
          gotbeg = 1;
        }
      else if (gotbeg == 1 && strncmp(buf1, "=yend ", 6) == 0)
        {
          p = strstr(buf1, "crc32=");
          if (p)
            sscanf(p + 6, "%x", &crc1);
          break;
        }
      else if (gotbeg == 1)
        {
          len = strlen(buf1);
          /* strip the CR LF */
          if (len > 2 && buf1[len - 1])
            {
              buf1[len - 1] = '\0';
              len--;
            }
          p = buf1;
          while (*p)
            {
              c = *p++;
              if (c == '=')
                {
                  c = *p++;
                  if (c == 0)
                    break; /* can't have escape char as last char in line */
                  c = (unsigned char) (c - 64);
                }

              c = (unsigned char) (c - 42);
              *p2++ = c;
              /* flush when buffer full */
              if (++outlen >= sizeof(buf2))
                {
                  crc = crc32(crc, (unsigned char*)buf2, outlen);
                  out->write(buf2, outlen);
                  p2 = buf2;
                  outlen = 0;
                }
            }
        }
    }
  /* flush remaining data */
  if (outlen)
  {
    crc = crc32(crc, (unsigned char*)buf2, outlen);
    out->write(buf2, outlen);
  }

  return (crc == crc1);
}

bool
compression::inflate_zlib(std::stringstream *source, std::stringstream *dest,
    const CompressionType& compression)
{
  int ret = Z_DATA_ERROR;
  size_t have;
  z_stream strm;
  char in[MEMCHUNK];
  char out[MEMCHUNK];

  /* allocate inflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.avail_in = 0;
  strm.next_in = Z_NULL;

  if (compression == HEADER_COMPRESS_XZVER)
    ret = inflateInit2(&strm, -MAX_WBITS); /* use -MAX_WBITS to indicate gzip style */

  if (compression == HEADER_COMPRESS_XFEATURE
     || compression == HEADER_COMPRESS_DIABLO)
    ret = inflateInit(&strm);

  if (ret != Z_OK)
    return ret;

  /* decompress until deflate stream ends or end of file */
  do
    {
      strm.avail_in = source->readsome(in, MEMCHUNK);
      if (strm.avail_in < 0) strm.avail_in = 0;
      if (source->fail())
      {
        (void) inflateEnd(&strm);
        return Z_ERRNO;
      }
      if (strm.avail_in == 0)
        break;
      strm.next_in = (unsigned char*) in;

      /* run inflate() on input until output buffer not full */
      do
      {
        strm.avail_out = MEMCHUNK;
        strm.next_out = (unsigned char*) out;
        ret = inflate(&strm, Z_NO_FLUSH);
        assert(ret != Z_STREAM_ERROR);
        /* state not clobbered */
        switch (ret)
        {
          case Z_NEED_DICT:
            ret = Z_DATA_ERROR; /* and fall through */
          case Z_DATA_ERROR:
          case Z_MEM_ERROR:
            (void) inflateEnd(&strm);
            return ret;
        }
        have = MEMCHUNK - strm.avail_out;
        dest->write(out, have);
      }
      while (strm.avail_out == 0);

      /* done when inflate() says it's done */
    }
  while (ret != Z_STREAM_END);

  /* clean up and return */
  (void) inflateEnd(&strm);
  return ret == Z_STREAM_END ? true : false;
}

void compression::inflate_gzip (std::stringstream* stream, std::stringstream* out)
{
  std::string line;
  std::istringstream in_str(stream->str());

  while (std::getline(in_str, line, '\r'))
  {
    StringView str(line.c_str());
    if (str.str[str.len - 1] == '.')
    {
      str.rtruncate(1);
      if (str.len != 0)
        *out<<str<<"\n";
      break;
    }
  }
  stream->clear();
}
