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
#include <string>
#include <vector>
#include <string.h>
#include <glib/gi18n.h>
#include <locale.h>
#include <gmime/gmime.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/log.h>
#include "utf8-utils.h"

using namespace pan;

namespace
{
  struct LocaleStruct
  {
    const char *locale, *charset;
  }
  locales[] =
  {
    {"en_US",        "ISO-8859-1"},
    {"pt_BR",        "ISO-8859-1"},
    {"ca_ES",        "ISO-8859-15"},
    {"zh_CN.GB2312", "gb2312"},
    {"zh_TW.Big5",   "big5"},
    {"cs_CZ",        "ISO-8859-2"},
    {"da_DK",        "ISO-8859-1"},
    {"de_DE",        "ISO-8859-15"},
    {"nl_NL",        "ISO-8859-15"},
    {"et_EE",        "ISO-8859-15"},
    {"fi_FI",        "ISO-8859-15"},
    {"fr_FR",        "ISO-8859-15"},
    {"el_GR",        "ISO-8859-7"},
    {"hu_HU",        "ISO-8859-2"},
    {"it_IT",        "ISO-8859-15"},
    {"ja_JP",        "ISO-2022-jp"},
    {"ko_KR",        "euc-kr"},
    {"lv_LV",        "ISO-8859-13"},
    {"lt_LT",        "ISO-8859-13"},
    {"no_NO",        "ISO-8859-1"},
    {"pl_PL",        "ISO-8859-2"},
    {"pt_PT",        "ISO-8859-15"},
    {"ro_RO",        "ISO-8859-2"},
    {"ru_RU",        "KOI8-R"},
    {"ru_SU",        "ISO-8859-5"},
    {"sk_SK",        "ISO-8859-2"},
    {"es_ES",        "ISO-8859-15"},
    {"sv_SE",        "ISO-8859-1"},
    {"tr_TR",        "ISO-8859-9"},
    {"uk_UK",        "KOI8-U"}
  };

  /* find_locale_index_by_locale:
   * finds the longest fit so the one who has en_GB will get en_US if en_GB
   * is not defined.
   * This function is lifted from Balsa.
   */
  gint
  get_closest_locale (void)
  {
    const char * locale = setlocale (LC_CTYPE, NULL);
    guint i, j, maxfit = 0, maxpos = 0;

    g_return_val_if_fail (locale != NULL, -1);

    if (!locale || strcmp(locale, "C") == 0)
      return 0;

    for (i = 0; i < G_N_ELEMENTS(locales); i++) {
      for (j=0; locale[j] && locales[i].locale[j] == locale[j]; j++) ;
      if (j > maxfit) {
        maxfit = j;
        maxpos = i;
      }
    }

    return maxpos;
  }

  const char * PAN_DEFAULT_CHARSET = "ISO-8859-1";

  const char *
  get_charset_from_locale (void)
  {
    gint loc_idx = get_closest_locale ();
    return loc_idx != -1 ? locales[loc_idx].charset : PAN_DEFAULT_CHARSET;
  }
}

std::string
pan :: clean_utf8 (const StringView& in_arg)
{
  StringView in (in_arg);
  std::string out;

  const char *end;
  while (!g_utf8_validate (in.str, in.len, &end)) {
    const gssize good_len (end - in.str);
    out.append (in.str, good_len);
    in.eat_chars (good_len + 1);
    out += '?';
  }

  out.append (in.str, in.len);
  g_assert (g_utf8_validate (out.c_str(), out.size(), NULL));
  return out;
}

std::string
pan :: header_to_utf8 (const StringView  & header,
                       const char        * fallback_charset1,
                       const char        * fallback_charset2)
{
  std::string s = content_to_utf8 (header, fallback_charset1, fallback_charset2);
  if (header.strstr ("=?")) {
    char * decoded (g_mime_utils_header_decode_text (NULL, s.c_str()));
    s = clean_utf8 (decoded);
    g_free (decoded);
  }
  return s;
}


std::string
pan :: mime_part_to_utf8 (GMimePart     * part,
                          const char    * fallback_charset)
{
  std::string ret;

  g_return_val_if_fail (GMIME_IS_PART(part), ret);
  const char * charset =
    g_mime_object_get_content_type_parameter (GMIME_OBJECT (part), "charset");
  GMimeDataWrapper * content = g_mime_part_get_content (part);
  GMimeStream *stream = g_mime_stream_mem_new ();

  g_mime_data_wrapper_write_to_stream (content, stream);
  GByteArray *buf = ((GMimeStreamMem *) stream)->buffer;
  ret = content_to_utf8 (StringView ((const char *) buf->data, buf->len),
    charset, fallback_charset);

  g_object_unref (stream);

  return ret;
}

std::string
pan :: content_to_utf8 (const StringView  & content,
                        const char        * fallback_charset1,
                        const char        * fallback_charset2)
{
  std::string ret;

  const StringView c1 (fallback_charset1);
  const StringView c2 (fallback_charset2);

  if (!content.empty())
  {
    // is it's already valid utf8?
    // only use this if there isn't a fallback charset,
    // since some other charsets (like utf-7) are also utf-8 clean...
    if (c1.empty() && c2.empty() && g_utf8_validate (content.str, content.len, NULL))
      ret.assign (content.str, content.len);

    // iterate through the charsets and try to convert to utf8.
    if (ret.empty())
    {
      // build a list of charsets to try
      size_t n = 0;
      const char* encodings[4];
      if (!c1.empty()) encodings[n++] = c1.str;
      if (!c2.empty()) encodings[n++] = c2.str;
      encodings[n++] = "CURRENT"; // try these when the user-supplied fallbacks fail...
      encodings[n++] = "ISO-8859-15";

      // try each charset in turn
      for (size_t i=0; i!=n; ++i) {
        char * tmp = g_convert (content.str, content.len, "UTF-8", encodings[i], nullptr, nullptr, NULL);
        if (tmp) {
          ret = tmp;
          g_free (tmp);
          break;
        }
      }
    }

    // if we couldn't figure it out, strip out all the non-utf8 and hope for the best.
    if (ret.empty())
      ret.assign (content.str, content.len);
    if (!g_utf8_validate (ret.c_str(), ret.size(), NULL)) {
      ret = clean_utf8 (ret);
      Log::add_err (_("Couldn't determine article encoding.  Non-UTF8 characters were removed."));
    }
  }

  return ret;
}
