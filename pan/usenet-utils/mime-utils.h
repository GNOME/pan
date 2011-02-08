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
#include <glib/gtypes.h>
#include <gmime/gmime-filter.h>
#include <gmime/gmime-stream.h>
#include <gmime/gmime-message.h>
#include <pan/general/string-view.h>

namespace pan
{
  /**
   * Utilities to build and parse GMimeMesasges.
   * 
   * Most of nastiness this is to handle Usenet's use of chainging together
   * multiple articles as parts of a whole.  This code tries to build
   * a multipart GMimeMessage from multiple posts when necessary, and to
   * also handle Usenet's loose standards for uu/yenc by checking each line
   * to separate the encoded stuff from text.
   */
  struct mime
  {
    static GMimeMessage *
    construct_message (GMimeStream ** istreams,
                         int            qty);


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

}



#endif
