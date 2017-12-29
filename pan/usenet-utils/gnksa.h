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

#ifndef __GNKSA_H__
#define __GNKSA_H__

#include <string>
#include <ctime>
#include <pan/usenet-utils/MersenneTwister.h>

namespace pan
{
   class StringView;

   /**
    * Utilities used for adhering to the Good Net-Keeping Seal of Approval guidelines.
    * @ingroup usenet_utils
    */
   class GNKSA
   {
      public:

         enum
         {
            // success/undefined error
            OK                       = 0,
            INTERNAL_ERROR           = 1,

            // general syntax
            LANGLE_MISSING           = 100,
            RANGLE_MISSING           = 101,
            LPAREN_MISSING           = 102,
            RPAREN_MISSING           = 103,
            ATSIGN_MISSING           = 104,

            // FQDN checks
            SINGLE_DOMAIN            = 200,
            INVALID_DOMAIN           = 201,
            ILLEGAL_DOMAIN           = 202,
            UNKNOWN_DOMAIN           = 203,
            INVALID_FQDN_CHAR        = 204,
            ZERO_LENGTH_LABEL        = 205,
            ILLEGAL_LABEL_LENGTH     = 206,
            ILLEGAL_LABEL_HYPHEN     = 207,
            ILLEGAL_LABEL_BEGNUM     = 208,
            BAD_DOMAIN_LITERAL       = 209,
            LOCAL_DOMAIN_LITERAL     = 210,
            RBRACKET_MISSING         = 211,

            // localpart checks
            LOCALPART_MISSING        = 300,
            INVALID_LOCALPART        = 301,
            ZERO_LENGTH_LOCAL_WORD   = 302,

            // realname checks
            ILLEGAL_UNQUOTED_CHAR    = 400,
            ILLEGAL_QUOTED_CHAR      = 401,
            ILLEGAL_ENCODED_CHAR     = 402,
            BAD_ENCODE_SYNTAX        = 403,
            ILLEGAL_PAREN_PHRASE     = 404,
            ILLEGAL_PAREN_CHAR       = 405,
            INVALID_REALNAME         = 406,
            ILLEGAL_PLAIN_PHRASE     = 407,
         };

         static StringView get_short_author_name (const StringView& full);

         static int  check_from      (const StringView  & from,
                                      bool                strict);

         static int  do_check_from   (const StringView  & from_header,
                                      StringView        & setme_addr,
                                      StringView        & setme_name,
                                      bool                strict);

         static int  check_domain    (const StringView  & domain);

         static std::string remove_broken_message_ids_from_references (const StringView& references);

         // GNKSA rule 7.  986 ==  998 chars - 12 for "References: "
         static std::string trim_references (const StringView& refs, size_t cutoff=986u);

         static std::string generate_references (const StringView& references,
                                                 const StringView& message_id);

         static std::string generate_message_id_from_email_address (const StringView& email);

         static std::string generate_message_id (const StringView& domain);

      public:

         enum SigType
         {
            SIG_NONE,
            SIG_STANDARD,
            SIG_NONSTANDARD
         };

         static SigType is_signature_delimiter    (const StringView& line);

         static SigType find_signature_delimiter  (const StringView& text,
                                                   int             & setme_index);

         bool  remove_signature (StringView& text);

   }; // class GNKSA

}; // namespace

#endif
