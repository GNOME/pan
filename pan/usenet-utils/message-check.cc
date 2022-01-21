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

#include <config.h>
#include <ctype.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include "gnksa.h"
#include "message-check.h"
#include "text-massager.h"
#include "mime-utils.h"

using namespace pan;

/***
****  PRIVATE UTILITIES
***/

namespace
{
   typedef std::set<std::string> unique_strings_t;
   typedef std::vector<std::string> strings_t;

   // Find and return a list of NNTP groups to send to
   void
   get_nntp_rcpts (const StringView& to, quarks_t& setme)
   {
      StringView token, myto(to);
      while (myto.pop_token (token, ','))
         setme.insert (token.to_string());
   }

   /***
   ****  OUTGOING MESSAGE CHECKS
   ***/

#if 0
  std::string
  strip_attribution_and_signature (const StringView& body_in, GMimeMessage * message)
  {
      std::string body (body_in.to_string());

      // strip attribution
      const char * attribution = g_mime_object_get_header ((GMimeObject *) message, PAN_ATTRIBUTION);
      if (attribution && *attribution)
      {
         std::string::size_type attrib_start_pos = body.find (attribution);
         if (attrib_start_pos != std::string::npos)
         {
            // the +2 is to trim out the following carriage returns
            const int attrib_len (::strlen(attribution) + 2);
            body.erase (attrib_start_pos, attrib_len);
         }
      }

      // strip out the signature
      char * sig_delimiter = 0;
// FIXME
      if (pan_find_signature_delimiter (body->str, &sig_delimiter) != SIG_NONE)
      {
         ccc ; g_string_truncate (body, sig_delimiter - body->str);

         pan_g_string_strstrip (body);
      }

      return body;
  }
#endif

  /**
   * Check to see if the user is top-posting.
   */
  void
  check_topposting (unique_strings_t       & errors,
                    MessageCheck::Goodness & goodness,
                    const TextMassager     & tm,
                    const StringView       & body,
                    GMimeMessage           * message)
  {
    // if it's not a reply, then top-posting check is moot
    if (g_mime_object_get_header ((GMimeObject *) message, "References") == NULL)
      return;

    bool quoted_found (false);
    bool original_found_after_quoted (false);
    StringView v(body), line;
    while (v.pop_token (line, '\n')) {
      if (line == "-- ") // signature reached
        break;
      if (tm.is_quote_character (g_utf8_get_char (line.str))) // check for quoted
        quoted_found = true;
      else if (quoted_found) { // check for non-quoted after quoted
        line.trim ();
        original_found_after_quoted = !line.empty();
        if (original_found_after_quoted)
          break;
      }
    }

    if (quoted_found && !original_found_after_quoted) {
      goodness.raise_to_warn ();
      errors.insert (_("Warning: Reply seems to be top-posted."));
    }
  }

  /**
   * Check to see if the signature (if found) is within the McQuary limit of
   * four lines and 80 columns per line.
   */
  void check_signature (unique_strings_t       & errors,
                        MessageCheck::Goodness & goodness,
                        const StringView       & body)
  {
    int sig_point (0);
    const GNKSA::SigType sig_type (GNKSA::find_signature_delimiter (body, sig_point));

    if (sig_type == GNKSA::SIG_NONE)
      return;

    if (sig_type == GNKSA::SIG_NONSTANDARD)
    {
      goodness.raise_to_warn ();
      errors.insert (_("Warning: The signature marker should be \"-- \", not \"--\"."));
    }

    // how wide and long is the signature?
    int sig_line_qty (-1);
    int too_wide_qty (0);
    StringView line, sig(body);
    sig.eat_chars (sig_point);
    while (sig.pop_token (line, '\n')) {
      ++ sig_line_qty;
      if (line.len > 80)
        ++too_wide_qty;
    }

    if (sig_line_qty == 0)
    {
      goodness.raise_to_warn ();
      errors.insert (_("Warning: Signature prefix with no signature."));
    }
    if (sig_line_qty > 4)
    {
      goodness.raise_to_warn ();
      errors.insert (_("Warning: Signature is more than 4 lines long."));
    }
    if (too_wide_qty != 0)
    {
      goodness.raise_to_warn ();
      errors.insert (_("Warning: Signature is more than 80 characters wide."));
    }
  }


  /**
   * Simple check to see if the body is too wide.  Any text after the
   * signature prefix is ignored in this test.
   */
  void check_wide_body (unique_strings_t       & errors,
                        MessageCheck::Goodness & goodness,
                        const StringView       & body)
  {
    int too_wide_qty (0);

    StringView v(body), line;
    while (v.pop_token (line, '\n')) {
      if (line == "-- ")
        break;
      if (line.len > 80)
        ++too_wide_qty;
    }

    if (too_wide_qty) {
      char buf[1024];
      g_snprintf (buf, sizeof(buf), ngettext(
                  "Warning: %d line is more than 80 characters wide.",
                  "Warning: %d lines are more than 80 characters wide.", too_wide_qty), too_wide_qty);
      errors.insert (buf);
      goodness.raise_to_warn ();

    }
  }

  /**
   * Check to see if the article appears to be empty.
   * Any text after the signature prefix is ignored in this test.
   */
  void check_empty (unique_strings_t       & errors,
                    MessageCheck::Goodness & goodness,
                    const StringView       & body)
  {
    StringView v(body), line;
    while (v.pop_token (line, '\n')) {
      if (line == "-- ") // sig reached;
        break;
      line.trim ();
      if (!line.empty()) // found text
        return;
    }

    errors.insert (_("Error: Message is empty."));
    goodness.raise_to_refuse ();
  }

  /**
   * Check to see how much original content is in this message, opposed
   * to quoted content.  Any text after the signature prefix is ignored
   * in this test.
   *
   * (1) count all the lines beginning with the quoted prefix.
   * (2) count all the nonempty nonsignature lines.  These are the orignal lines.
   * (3) if the ratio of original/quoted is 20% or less, warn.
   * (4) if the ratio of original/quoted is 0%, warn louder.
   */
  void
  check_mostly_quoted (unique_strings_t        & errors,
                       MessageCheck::Goodness  & goodness,
                       const StringView        & body)
  {
    int total(0), unquoted(0);
    StringView v(body), line;
    while (v.pop_token (line, '\n')) {
      if (line == "-- ") break; // sig reached
      line.trim ();
      if (line.empty())
        continue;
      ++total;
      if (*line.str != '>')
        ++unquoted;
    }

    if (total!=0 && ((int)(100.0*unquoted/total)) < 20)
    {
      goodness.raise_to_warn ();
      errors.insert (unquoted==0
        ?  _("Warning: The message is entirely quoted text!")
        :  _("Warning: The message is mostly quoted text."));
    }
  }

  /**
   * Check to see if the article appears to only have quoted text.  If this
   * appears to be the case, we will refuse to post the message.
   *
   * (1) Get mutable working copies of the article body and the attribution
   *     string.
   *
   * (2) Replace carriage returns in both the calculated attribution string
   *     and a temporary copy of the message body, so that we don't have to
   *     worry whether or not the attribution line's been wrapped.
   *
   * (3) Search for an occurance of the attribution string in the body.  If
   *     it's found, remove it from the temporary copy of the body so that
   *     it won't affect our line counts.
   *
   * (4) Of the remaining body, look for any nonempty lines before the signature
   *     file that don't begin with the quote prefix.  If such a line is found,
   *     then the message is considered to not be all quoted text.
   *
   */
  void check_all_quoted (unique_strings_t        & errors,
                         MessageCheck::Goodness  & goodness,
                         const TextMassager      & tm,
	                 const StringView        & body,
		         const StringView        & attribution)
  {
    if (body.empty())
      return;

    // strip out the attribution, if any
    std::string s (body.str, body.len);

    if (!attribution.empty()) {
      std::string::size_type pos = s.find (attribution.str, attribution.len);
      if (pos != std::string::npos)
        s.erase (pos, attribution.len+2); // the +2 is to trim out the following carriage returns
    }

    StringView v(s), line;
    while (v.pop_token (line, '\n')) {
      if (line == "-- ") break;
      line.trim ();
      if (line.empty()) continue;
      if (!tm.is_quote_character (g_utf8_get_char (line.str))) return; // found new content
    }

    errors.insert (_("Error: Message appears to have no new content."));
    goodness.raise_to_refuse ();
  }


  void check_body (unique_strings_t       & errors,
                   MessageCheck::Goodness & goodness,
                   const TextMassager     & tm,
                   GMimeMessage           * message,
                   const StringView       & body,
                   const StringView       & attribution)
  {
    check_empty         (errors, goodness, body);
    check_wide_body     (errors, goodness, body);
    check_signature     (errors, goodness, body);
    check_mostly_quoted (errors, goodness, body);
    check_all_quoted    (errors, goodness, tm, body, attribution);
    check_topposting    (errors, goodness, tm, body, message);
  }

  void
  check_followup_to (unique_strings_t        & errors,
                     MessageCheck::Goodness  & goodness,
                     const quarks_t          & groups_our_server_has,
                     const quarks_t          & group_names)
  {
    const Quark poster ("poster");

    // check to make sure all the groups exist
    foreach_const (quarks_t, group_names, it) {
      if (*it == poster)
        continue;
      else if (!groups_our_server_has.count (*it)) {
        goodness.raise_to_warn ();
        char * tmp = g_strdup_printf (
          _("Warning: The posting profile's server doesn't carry newsgroup\n"
            "\t\"%s\".\n"
            "\tIf the group name is correct, switch profiles in the \"From:\"\n"
            "\tline or edit the profile with \"Edit|Manage Posting Profiles\"."), it->c_str());
        errors.insert (tmp);
        g_free (tmp);
      }
    }

    // warn if too many followup-to groups
    if (group_names.size() > 5u) {
      errors.insert (_("Warning: Following-Up to too many groups."));
      goodness.raise_to_warn ();
    }
  }

  void check_subject (unique_strings_t       & errors,
                      MessageCheck::Goodness & goodness,
                      const StringView       & subject)
  {
    if (subject.empty()) {
      goodness.raise_to_refuse ();
      errors.insert (_("Error: No Subject specified."));
    }
  }

  void check_groups (unique_strings_t        & errors,
                     MessageCheck::Goodness  & goodness,
                     const quarks_t          & groups_our_server_has,
                     const quarks_t          & group_names,
                     bool                      followup_to_set)
  {
    // make sure all the groups exist and are writable
    foreach_const (quarks_t, group_names, it)
    {
      if (!groups_our_server_has.count (*it))
      {
        goodness.raise_to_warn ();
        char * tmp = g_strdup_printf (
          _("Warning: The posting profile's server doesn't carry newsgroup\n"
            "\t\"%s\".\n"
            "\tIf the group name is correct, switch profiles in the \"From:\"\n"
            "\tline or edit the profile with \"Edit|Manage Posting Profiles\"."), it->c_str());
        errors.insert (tmp);
        g_free (tmp);
      }
#if 0
      if (data.get_group_permission (*it) == 'n')
      {
        goodness.raise_to_warn ();
        char buf[1024];
        g_snprintf (buf, sizeof(buf), _("Warning: Group \"%s\" is read-only."), it->c_str());
        errors.insert (buf);
      }
#endif
    }

    if (group_names.size() >= 10u) // refuse if far too many groups
    {
        goodness.raise_to_refuse ();
        errors.insert (_("Error: Posting to a very large number of groups."));
    }
    else if (group_names.size() > 5) // warn if too many groups
    {
        goodness.raise_to_warn ();
        errors.insert (_("Warning: Posting to a large number of groups."));
    }

    // warn if too many groups and no followup-to
    if (group_names.size()>2u && !followup_to_set)
    {
        goodness.raise_to_warn ();
        errors.insert (_("Warning: Crossposting without setting Followup-To header."));
    }
  }
}

void
MessageCheck :: message_check (const GMimeMessage * message_const,
                               const StringView   & attribution,
                               const quarks_t     & groups_our_server_has,
                               unique_strings_t   & errors,
                               Goodness           & goodness,
                               bool                 binpost)
{

  goodness.clear ();
  errors.clear ();

  // we only use accessors in here, but the GMime API doesn't allow const...
  GMimeMessage * message (const_cast<GMimeMessage*>(message_const));

  // check the subject...
  check_subject (errors, goodness, g_mime_message_get_subject (message));

  // check the author...
  if (GNKSA::check_from (g_mime_object_get_header ((GMimeObject *) message, "From"), true)) {
    errors.insert (_("Error: Bad email address."));
    goodness.raise_to_warn ();
  }

  // check the body...
  TextMassager tm;
  gboolean is_html;
  char * body = pan_g_mime_message_get_body (message, &is_html);
  if (is_html && !binpost) {
    errors.insert (_("Warning: Most newsgroups frown upon HTML posts."));
    goodness.raise_to_warn ();
  }

  if (!binpost)
    check_body (errors, goodness, tm, message, body, attribution);
  g_free (body);

  // check the optional followup-to...
  bool followup_to_set (false);
  const char * cpch = g_mime_object_get_header ((GMimeObject *) message, "Followup-To");
  if (!binpost)
  {
    if (cpch && *cpch) {
      quarks_t groups;
      get_nntp_rcpts (cpch, groups);
      followup_to_set = !groups.empty();
      check_followup_to (errors, goodness, groups_our_server_has, groups);
    }
  } else
    followup_to_set = true;

  // check the groups...
  size_t group_qty (0);
  cpch = g_mime_object_get_header ((GMimeObject *) message, "Newsgroups");
  if (cpch && *cpch) {
    quarks_t groups;
    get_nntp_rcpts (cpch, groups);
    check_groups (errors, goodness, groups_our_server_has, groups, followup_to_set);
    group_qty = groups.size ();
  }

  // one last error check
  InternetAddressList * list (g_mime_message_get_addresses (message, GMIME_ADDRESS_TYPE_TO));
  const int n_to (internet_address_list_length (list));
  if (!group_qty && !n_to) {
    errors.insert (_("Error: No Recipients."));
    goodness.raise_to_refuse ();
  }
}
