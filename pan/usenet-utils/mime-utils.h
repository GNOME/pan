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

#ifndef _UtilMime_h_
#define _UtilMime_h_

#include <vector>
#include <glib.h>
#include <gmime/gmime-filter.h>
#include <gmime/gmime-stream.h>
#include <gmime/gmime-message.h>
#include <pan/general/string-view.h>

#ifdef HAVE_GPGME
  #include <gpgme.h>
  #include <pan/gui/gpg.h>
#endif

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

/***
**** GPG
***/

#define GPG_MARKER_BEGIN            "-----BEGIN PGP MESSAGE-----"
#define GPG_MARKER_BEGIN_LEN        27
#define GPG_MARKER_END              "-----END PGP MESSAGE-----"
#define GPG_MARKER_END_LEN          25
#define GPG_MARKER_SIGNED_BEGIN     "-----BEGIN PGP SIGNED MESSAGE-----"
#define GPG_MARKER_SIGNED_BEGIN_LEN 34
#define GPG_MARKER_SIGNED_END       "-----END PGP SIGNATURE-----"
#define GPG_MARKER_SIGNED_END_LEN   27

namespace pan
{
#ifdef HAVE_GPGME
  GMimeStream* gpg_decrypt_and_verify (GPGSignersInfo& signer_info, GPGDecErr& info, GMimeStream* s, int index=0, GMimeObject* parent=0);
  GMimeMessage* message_add_signed_part (const std::string& uid, const std::string& body_str, GMimeMessage* body, GPGEncErr& fail);
  std::string gpg_encrypt(const std::string& uid, const std::string& body, GPGEncErr& fail);
  std::string gpg_encrypt_and_sign(const std::string& uid, const std::string& body, GPGEncErr& fail);
#endif

  /**
   * Utilities to build and parse GMimeMessages.
   *
   * Most of nastiness this is to handle Usenet's use of chainging together
   * multiple articles as parts of a whole.  This code tries to build
   * a multipart GMimeMessage from multiple posts when necessary, and to
   * also handle Usenet's loose standards for uu/yenc by checking each line
   * to separate the encoded stuff from text.
   */
  struct mime
  {
#ifdef HAVE_GPGME
    static GMimeMessage *
    construct_message (GMimeStream      ** istreams,
                         int              qty,
                         GPGSignersInfo & signer_info,
                         GPGDecErr      & gpgerr);
#else
    static GMimeMessage *
    construct_message (GMimeStream      ** istreams,
                         int              qty);
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
  void pan_g_mime_message_add_recipients_from_string (GMimeMessage *message, GMimeRecipientType type, const char *string);
  void pan_g_mime_message_set_message_id (GMimeMessage *msg, const char *mid);

}



#endif
