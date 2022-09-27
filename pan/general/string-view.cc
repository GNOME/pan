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
#include <algorithm>
#include <cassert>
#include <cctype>
#include <string>
#include <iostream>
#include <glib.h>
#include "debug.h"
#include "messages.h"
#include "string-view.h"

using namespace pan;

/***
****  Length-bounded string utilities
***/

int
StringView :: strcmp (const char * str_a,
                      size_t       str_a_len,
                      const char * str_b,
                      size_t       str_b_len)
{
   int retval (memcmp (str_a, str_b, std::min(str_a_len,str_b_len)));
   if (!retval)
      retval = str_a_len - str_b_len;
   return retval;
}

char*
StringView :: strrchr (const char * haystack,
                       size_t       haystack_len,
                       char         needle)
{
   const char * pch (nullptr);

   pan_return_val_if_fail (haystack!=NULL || haystack_len==0, NULL);

   if (haystack_len)
   {
      pch = haystack + haystack_len - 1;
      for (; pch!=haystack; --pch)
         if (*pch == needle)
            break;
      if (*pch != needle)
         pch = NULL;
   }

   return (char*) pch;
}

int
StringView :: strncpy (char        * target,
                       size_t        target_size,
                       const char  * source_str,
                       size_t        source_len)
{
   /* sanity clause */
   pan_return_val_if_fail (target!=NULL || target_size==0, 0);
   pan_return_val_if_fail (source_str!=NULL || source_len==0, 0);

   const int len (std::min (target_size-1, source_len));
   memcpy (target, source_str, len);
   target[len] = '\0';
   return len;
}

char*
StringView :: strstr (const char * haystack,
                      size_t       haystack_len,
                      const char * needle,
                      size_t       needle_len)
{
  if (!haystack)
    return NULL;

  assert (needle != NULL);

  if (needle_len == 0)
    return (char*) haystack;

  if (haystack_len < needle_len)
    return NULL;

  const char * s = haystack;
  const char * end = s + haystack_len;
  while ((s = strchr (s, end-s, *needle))) {
    if (((end-s) >= (int)needle_len) && !memcmp(s,needle,needle_len))
      return (char*) s;
    ++s;
  }

  return nullptr;
}

char*
StringView :: strpbrk (const char * haystack,
                       size_t       haystack_len,
                       const char * needles)
{
   if (!haystack || !needles)
      return nullptr;

   for ( ; haystack_len && *haystack; ++haystack, --haystack_len )
      for (const char *p=needles; *p; ++p)
         if (*haystack == *p)
            return (char *) haystack;

   return nullptr;
}


/****
*****
****/

void
StringView :: ltrim ()
{
  // strip leading whitespace
  if (!empty()) {
    const char *p(str), *end(p+len);
    while (p < end) {
      if (!g_unichar_isspace (g_utf8_get_char (p)))
        break;
      p = g_utf8_next_char (p);
    }
    eat_chars (p-str);
  }
}

void
StringView :: rtrim ()
{
  // strip trailing whitespace
  if (!empty()) {
    const char *pch, *prev (str + len);
    while ((pch = g_utf8_find_prev_char (str, prev)))
      if (g_unichar_isspace (g_utf8_get_char (pch)))
        prev = pch;
      else
        break;
    len = prev - str;
  }
}

void
StringView :: trim ()
{
  ltrim ();
  rtrim ();
}

bool
StringView :: pop_token (StringView& token, char delimiter)
{
   const bool got_token (len != 0);
   const char * pch = strchr (delimiter);
   if (pch) {
     token.str = str;
     token.len = pch - str;
     len -= token.len+1;
     str += token.len+1;
   } else {
     token.str = str;
     token.len = len;
     str = nullptr;
     len = 0;
   }
   return got_token;
}

bool
StringView :: pop_last_token (StringView& token, char delimiter)
{
  bool got_token (len != 0);
  const char * pch = strrchr (delimiter);
  if (pch) {
    token.str = pch + 1;
    token.len = str + len - token.str;
    len = pch - str;
  } else {
    token.str = str;
    token.len = len;
    str = nullptr;
    len = 0;
  }

   return got_token;
}

void
StringView :: substr (const char * begin, const char * end, StringView& setme) const
{
  if (!begin)
    begin = str;
  if (!end)
    end = str + len;

  setme.str = begin;
  setme.len = end-begin;
}

StringView
StringView :: substr (const char * begin, const char * end) const
{
  if (!begin)
    begin = str;
  if (!end)
    end = str + len;

  return StringView (begin, end-begin);
}

void
StringView :: eat_chars (size_t n)
{
  n = std::min (n, len);
  len -= n;
  str = len ? str+n : nullptr;
}

void
StringView :: truncate (size_t l)
{
  if (l<=len)
    len = l;
}

void
StringView :: rtruncate (size_t l)
{
  if (l<=len)
  {
    str += (len - l);
    len = l;
  }
}

std::ostream&
pan::operator<< (std::ostream& os, const pan::StringView& s)
{
  os.write (s.str, s.len);
  return os;
}
