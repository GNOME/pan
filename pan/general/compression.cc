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

  struct gzip_line {
    std::stringstream* stream;
    char *start;
    char *where;
    size_t remaining;
    size_t allocated;
    int eof;
  };

  void
  line_init(struct gzip_line *line, std::stringstream* stream)
  {
      line->stream = stream;
      line->allocated = LINELEN_MIN;
      line->where = line->start = (char*)calloc(1, line->allocated);
      line->remaining = 0;
      line->eof = 0;
  }

  int
  line_read(struct gzip_line *line, char **p)
  {
      char *where;
      char *lf = NULL;

      if (line->remaining != 0) {
          if (line->start != line->where) {
              memmove(line->start, line->where, line->remaining);
              lf = (char*)memchr(line->start, '\n', line->remaining);
          }
      }
      where = line->start + line->remaining;

      if (lf == NULL) {
          do {
              ssize_t count;
              if (where == line->start + line->allocated) {
                  size_t newsize = line->allocated * 2;
                  if (newsize > LINELEN_MAX)
                      newsize = LINELEN_MAX;
                  if (newsize == line->allocated) {
                      where = line->start;
                      line->eof = 1;
                  } else {
                      line->start = (char*)realloc(line->start, newsize);
                      where = line->start + line->allocated;
                      line->allocated = newsize;
                  }
              }

              do {
                  count = line->stream->readsome(where, line->allocated - (where - line->start));
              } while (count == -1);

              if (count < 0) {
                  count = 0;
              }

              if (count == 0) {
                  line->eof = 1;
                  lf = where;
                  where++;
                  break;
              }
              lf = (char*)memchr(where, '\n', count);
              where += count;
          } while (lf == NULL);
      }

      line->where = lf + 1;
      line->remaining = where - line->where;

      *lf = '\0';
      *p = line->start;
      if (line->eof)
          return lf - line->start;
      return lf - line->start + 1; /* <<=== length includes terminator '\0' */
  }


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
          std::cerr<<"crc loop "<<p<<"\n";
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

void compression::inflate_gzip (std::stringstream* stream, std::vector<std::string>& fillme)
{
  struct gzip_line g_line;
  size_t len;
  char* buf;
  char buf2[4096];

  line_init(&g_line, stream);

  std::stringstream dest, dest2;
  while (!g_line.eof) {
      while ((len = line_read(&g_line, &buf)) > 0) {
          buf[len-1] = '\n';
          dest.write(buf, len);
          if (len >= 3 && strncmp(buf + len - 1, ".", 1) == 0) {
              g_line.eof = 1;
              break;
          }
      }
  }

  std::cerr<<"inflate : "<<inflate_zlib(&dest, &dest2, HEADER_COMPRESS_XFEATURE)<<"\n\n";

  std::ofstream out ("/home/imhotep/compression/out");
    out << dest2.str();
    out.close();

  int cnt=0;
  while (!dest2.getline(buf2,4096).eof())
    {if (buf2) fillme.push_back(std::string(buf2));
    ++cnt;}

  stream->clear();

  std::cerr<<cnt<<"\n";



}
