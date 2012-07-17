/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2007  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2007 Calin Culianu <calin@ajvar.org>
 * Copyright (C) 2007 Charles Kerr <charles@rebelbase.com>
 *
 * This file is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
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
#include <cerrno>
#include <ostream>
#include <fstream>
extern "C" {
#  define PROTOTYPES
#  include <uulib/uudeview.h>
#  include <glib/gi18n.h>
};
#include <pan/general/worker-pool.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include "xzver-decoder.h"

using namespace pan;

/***
****
***/

using namespace pan;

XZVERDecoder :: XZVERDecoder (WorkerPool& pool) :
  Decoder(pool)
{
  _cnt = 0;
}

XZVERDecoder :: ~XZVERDecoder()
{
  disable_progress_update();
}

void
XZVERDecoder :: enqueue (TaskXOver  * task, TaskXOver::DataStream* stream, Data* data)
{

  _cnt = 0;

  disable_progress_update ();

  this->xtask = task;
  this->stream = stream;
  this->data = data;
  this->nntp = new NNTP(stream->server, "", "", 0);
  this->nntp->_group = stream->group;

  log_infos.clear();
  log_errors.clear();

  // gentlemen, start your saving...
  _worker_pool.push_work (this, task, false);
}

namespace
{
  char* build_cachename (char* buf, size_t len, const char* name)
  {
    const char * home(file::get_pan_home().c_str());
    g_snprintf(buf,len,"%s%c%s%c%s",home, G_DIR_SEPARATOR, "encode-cache", G_DIR_SEPARATOR, name);
    return buf;
  }
}

namespace
{
  unsigned long view_to_ul (const StringView& view)
  {
    unsigned long ul = 0ul;

    if (!view.empty()) {
      errno = 0;
      ul = strtoul (view.str, 0, 10);
      if (errno)
        ul = 0ul;
    }

    return ul;
  }
  uint64_t view_to_ull (const StringView& view)
  {
    uint64_t ul = 0ul;

    if (!view.empty()) {
      errno = 0;
      ul = g_ascii_strtoull (view.str, 0, 10);
      if (errno)
        ul = 0ul;
    }

    return ul;
  }

  bool header_is_nonencoded_utf8 (const StringView& in)
  {
    const bool is_nonencoded (!in.strstr("=?"));
    const bool is_utf8 (g_utf8_validate (in.str, in.len, 0));
    return is_nonencoded && is_utf8;
  }
}

void
XZVERDecoder :: on_nntp_batch_process (StringView   & line)
{
  unsigned int lines=0u;
  unsigned long bytes=0ul;
  uint64_t number=0;

  StringView subj, author, date, mid, tmp, xref;
  StringView& l = line;
  std::string ref;

  bool ok = !l.empty();
  ok = ok && l.pop_token (tmp, '\t');    if (ok) number = view_to_ull (tmp); tmp.clear();
  ok = ok && l.pop_token (subj, '\t');   if (ok) subj.trim ();
  ok = ok && l.pop_token (author, '\t'); if (ok) author.trim ();
  ok = ok && l.pop_token (date, '\t');   if (ok) date.trim ();
  ok = ok && l.pop_token (mid, '\t');    if (ok) mid.trim ();

  //handle multiple "References:"-message-ids correctly.
  ok = ok && l.pop_token (tmp, '\t');
  do
  {
    if (tmp.empty()) continue;
    if (tmp.front() == '<')
    {
      tmp.trim();
      ref += tmp;
      tmp.clear();
    } else break;
  } while ((ok = ok && l.pop_token (tmp, '\t'))) ;

                                         if (ok) bytes = view_to_ul (tmp); tmp.clear();
  ok = ok && l.pop_token (tmp, '\t');    if (ok) lines = view_to_ul (tmp);
  ok = ok && l.pop_token (xref, '\t');   if (ok) xref.trim ();

  if (xref.len>6 && !strncmp(xref.str,"Xref: ", 6)) {
    xref = xref.substr (xref.str+6, 0);
    xref.trim ();
  }

  // is this header corrupt?
  if (!number // missing number
      || subj.empty() // missing subject
      || author.empty() // missing author
      || date.empty() // missing date
      || mid.empty() // missing mid
      || mid.front()!='<') // corrupt mid
      /// Concerning bug : https://bugzilla.gnome.org/show_bug.cgi?id=650042
      /// Even if we didn't get a proper reference here, continue.
      //|| (!ref.empty() && ref.front()!='<'))
    return;

  // if news server doesn't provide an xref, fake one
  char * buf (0);
  if (xref.empty())
    xref = buf = g_strdup_printf ("%s %s:%"G_GUINT64_FORMAT,
                                  nntp->_server.c_str(),
                                  nntp->_group.c_str(),
                                  number);

  const char * fallback_charset = NULL; // FIXME
  const time_t time_posted = g_mime_utils_header_decode_date (date.str, NULL);

  data->xover_add (
    nntp->_server, nntp->_group,
    (header_is_nonencoded_utf8(subj) ? subj : header_to_utf8(subj,fallback_charset).c_str()),
    (header_is_nonencoded_utf8(author) ? author : header_to_utf8(author,fallback_charset).c_str()),
    time_posted, mid, StringView(ref), bytes, lines, xref);

  high = std::max (high, number);

  g_free (buf);
}

void
XZVERDecoder :: find_lines()
{
  StringView ret;

  while(!s_stream.eof())
  {
    std::string out;
    getline(s_stream, out);
    ret.assign(out);
    xtask->on_nntp_line_process(nntp, ret);
    _cnt++;
  }

}


// save article IN A WORKER THREAD to avoid network stalls
void
XZVERDecoder :: do_work()
{

  const int bufsz = 4096;
  char buf[bufsz];

  disable_progress_update();

  int res;
  if (((res = UUInitialize())) != UURET_OK)
    log_errors.push_back(_("Error initializing uulib")); // log error
  else
  {
    build_cachename(buf,sizeof(buf), "xzver_test");
    std::ofstream test (buf);
    test << stream->stream->str();
    test.close();

    // TODO mod uulib to use streams!

    UUSetOption (UUOPT_DESPERATE, 0, NULL);
    UUSetOption (UUOPT_IGNMODE, 1, NULL); // don't save file as executable
    UULoadFile (buf, 0, 0);
    UUDecodeFile (UUGetFileListItem (0), build_cachename(buf,sizeof(buf), "xzver_decoded"));
    UUCleanUp ();
  }

//  disable_progress_update();

  _strm.zalloc = Z_NULL;
  _strm.zfree = Z_NULL;
  _strm.opaque = Z_NULL;
  _strm.avail_in = 0;
  _strm.next_in = Z_NULL;

  _zret = inflateInit2(&_strm,-MAX_WBITS); //raw inflate
  if (_zret != Z_OK)
  {
    log_errors.push_back(_("Error initializing zlib deflate"));
    return;
  }

  std::ifstream headers;
  FILE * in = fopen(build_cachename(buf,sizeof(buf), "xzver_decoded"), "rb");

  if (!in)
  {
      char tmpbuf[2048];
      g_snprintf(tmpbuf, sizeof(tmpbuf), _("Error opening header file %s"), buf);
      log_errors.push_back(tmpbuf);
      return;
  }

  int ret;
  char fbuf[CHUNK];
  StringView line;

  do
  {
    size_t len = fread(fbuf, sizeof(char), CHUNK, in);
    if (len==0 || ferror(in)) break;
    ret = inflate_xzver (len, fbuf);
  } while (!feof(in) && _zret == Z_OK);

  find_lines();

  if (in) fclose(in);
  (void)inflateEnd(&_strm);

  xtask->setHigh(nntp->_server, high);

}

int
XZVERDecoder :: inflate_xzver (size_t len, char* buf)
{

  InflateChunk ret;

  _strm.avail_in = len;
  _strm.next_in = (unsigned char*)buf;

  /* run inflate() on input until output buffer not full */
  do {
      _strm.avail_out = CHUNK;
      _strm.next_out = ret.tmpbuf;
      _zret = inflate(&_strm, Z_NO_FLUSH);
      assert(_zret != Z_STREAM_ERROR);  /* state not clobbered */
      switch (_zret) {
      case Z_NEED_DICT:
          _zret = Z_DATA_ERROR;     /* and fall through */
      case Z_DATA_ERROR:
      case Z_MEM_ERROR:
          (void)inflateEnd(&_strm);
          return _zret;
      }

      s_stream<<ret.tmpbuf;

  } while (_strm.avail_out == 0);

  return _zret == Z_STREAM_END ? Z_OK : Z_STREAM_ERROR;

}


