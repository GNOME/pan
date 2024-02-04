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

#ifndef _UtilMime_h_
#define _UtilMime_h_

#include <config.h>
#include <vector>
#include <glib.h>
#include <gmime/gmime.h>
#include <gmime/gmime-filter.h>
#include <gmime/gmime-stream.h>
#include <gmime/gmime-message.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/gpg.h>

/***
**** YENC
***/

#define YENC_MARKER_BEGIN      "=ybegin"
#define YENC_MARKER_BEGIN_LEN  7
#define YENC_MARKER_PART       "=ypart"
#define YENC_MARKER_PART_LEN   6
#define YENC_MARKER_END        "=yend"
#define YENC_MARKER_END_LEN    5
#define YENC_TAG_PART          " part="
#define YENC_TAG_LINE          " line="
#define YENC_TAG_SIZE          " size="
#define YENC_TAG_NAME          " name="
#define YENC_TAG_BEGIN         " begin="
#define YENC_TAG_END           " end="
#define YENC_TAG_PCRC32        " pcrc32="
#define YENC_TAG_CRC32         " crc32="
#define YENC_FULL_LINE_LEN     256
#define YENC_HALF_LINE_LEN     128
#define YENC_ESC_NULL          "=@"
#define YENC_ESC_TAB           "=I"
#define YENC_ESC_LF            "=J"
#define YENC_ESC_CR            "=M"
#define YENC_ESC_ESC           "={"
#define YENC_SHIFT             42
#define YENC_QUOTE_SHIFT       64

#define NEEDS_DECODING(encoding) ((encoding == GMIME_CONTENT_ENCODING_BASE64) ||   \
                                 (encoding == GMIME_CONTENT_ENCODING_QUOTEDPRINTABLE))

namespace pan
{

#ifdef HAVE_GMIME_CRYPTO
  bool message_add_signed_part (const std::string& uid, const std::string& body_str, GMimeMessage* body);
  bool gpg_encrypt (const std::string& uid, const std::string& body_str, GMimeMessage* body, GPtrArray* rcp, bool sign);
  bool gpg_verify_mps (GMimeObject*, GPGDecErr&);
#endif
  /**
   * Utilities to build and parse GMimeMessages.
   *
   * Most of this nastiness is to handle Usenet's use of chainging together
   * multiple articles as parts of a whole.  This code tries to build
   * a multipart GMimeMessage from multiple posts when necessary, and to
   * also handle Usenet's loose standards for uu/yenc by checking each line
   * to separate the encoded stuff from text.
   */
  struct mime
  {
#ifdef HAVE_GMIME_CRYPTO
    static GMimeMessage *
    construct_message (GMimeStream      ** istreams,
                       unsigned int        qty,
                       GPGDecErr         &);
#else
    static GMimeMessage *
    construct_message (GMimeStream      ** istreams,
                       unsigned int        qty);
#endif
    static const char *
    get_charset (GMimeMessage * message);

    static void
    guess_part_type_from_filename (const char   * filename,
                                   const char  ** setme_type,
                                   const char  ** setme_subtype);

    static void
    remove_multipart_from_subject (const StringView    & subject,
                                   std::string         & setme);

    static void
    remove_multipart_part_from_subject (const StringView    & subject,
                                        std::string         & setme);

  };

  char *pan_g_mime_message_get_body (GMimeMessage *message, gboolean *is_html);
  void pan_g_mime_message_add_recipients_from_string (GMimeMessage *message, GMimeAddressType type, const char *string);
  std::string pan_g_mime_message_set_message_id (GMimeMessage *msg, const char *mid);

  extern iconv_t conv;
  extern bool iconv_inited;

  static char * __g_mime_iconv_strdup (iconv_t cd, const char *str, const char* charset=nullptr)
  {
    return g_mime_iconv_strndup(cd, str, strlen(str));
  }

}



#endif
