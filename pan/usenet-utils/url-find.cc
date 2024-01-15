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
#include <cctype>
#include "gnksa.h"
#include "url-find.h"

using namespace pan;
namespace {
  class fooregex {
    public:
      fooregex(): regex(NULL)
      {
        // RFC1738
        // unsafe in URL (always encoded):  {}|\^~[]`<>"
        // reserved for schemas: ;/?:@=&
        // % (hex encoding) # (fragment)
        // allowed: a-z A-Z 0-9 $-_.+!*'(),
        // imhotep : * removed ')' from allowed characters
        //           * added '~' & '-' to allowed characters
        regex = g_regex_new("(?:"
            "https?://|"
            "ftps?(?:://|\\.)|" //ftp:// ftp.
            "gemini://|" // gemini
            "news:|nntp:|"
            "www\\.|"
            "[[:alnum:]][[:alnum:]_+-\\.]*@" //email
          ")"
          "[" "[:alnum:]$_\\-\\.!+*()',%#~" ";:/?&=@" "]+" /* uri */,
          G_REGEX_OPTIMIZE, (GRegexMatchFlags)0, NULL);
      }
      ~fooregex()
      {
        g_regex_unref(regex);
      }
      operator GRegex*() {return regex;}

      GRegex *regex;
  };

  fooregex regex;
};

bool
pan :: url_find (const StringView& text, StringView& setme_url)
{
  if (text.empty())
    return false;

  GMatchInfo *match = NULL;

  if (!g_regex_match(regex, text.str, (GRegexMatchFlags)0, &match)) {
    g_match_info_free(match);
    return false;
  }

  int start,end;

  g_match_info_fetch_pos(match, 0, &start, &end);
  g_match_info_free(match);
  setme_url.assign(text.str+start, end - start);

  // for urls at the end of a sentence.
  if (!setme_url.empty() && strchr("?!.,", setme_url.back()))
    --setme_url.len;
  if (start > 0) {
    const char c = text.str[ start - 1 ];
    if (c == '\'' && c == setme_url.back() )
      --setme_url.len;
  }
  return true;
}

// This is a cheap little hack that should eventually be replaced
// with something more robust.
namespace pan {
bool url_findx (const StringView& text, StringView& setme_url);
}

bool
pan :: url_findx (const StringView& text, StringView& setme_url)
{
  if (text.empty())
    return false;

  char bracket (0);
  const char * start (nullptr);
  for (const char *pch (text.begin()), *end(text.end()); pch!=end; ++pch)
  {
    if (*pch=='h' && (end-pch>7) && !memcmp(pch,"http://",7)) {
      start = pch;
      break;
    }
    else if (*pch=='h' && (end-pch>8) && !memcmp(pch,"https://",8)) {
      start = pch;
      break;
    }
    else if (*pch=='f' && (end-pch>6) && !memcmp(pch,"ftp://", 6)) {
      start = pch;
      break;
    }
    if (*pch=='g' && (end-pch>9) && !memcmp(pch,"gemini://",9)) {
      start = pch;
      break;
    }
    else if (*pch=='n' && (end-pch>5) && !memcmp(pch,"news:",5)) {
      start = pch;
      break;
    }
    else if (*pch=='f' && (end-pch>5) && !memcmp(pch,"ftp.",4) && isalpha(pch[4])) {
      start = pch;
      break;
    }
    else if (*pch=='w' && (end-pch>5) && !memcmp(pch,"www.",4) && isalpha(pch[4])) {
      start = pch;
      break;
    }
    else if (*pch=='@') {
      const char *b, *e;
      for (b=pch; b!=text.str && !isspace(b[-1]); ) --b;
      for (e=pch; e!=text.end() && !isspace(e[1]); ) ++e;
      StringView v (b, e+1-b);
      while (!v.empty() && strchr(">?!.,", v.back())) --v.len;
      if (GNKSA::check_from(v,false) == GNKSA::OK) {
        setme_url = v;
        return true;
      }
    }
  }

  if (!start)
    return false;

  if (start != text.begin()) {
    char ch (start[-1]);
    if (ch == '[') bracket = ']';
    else if (ch == '<') bracket = '>';
  }

  const char * pch;
  for (pch=start; pch!=text.end(); ++pch) {
    if (bracket) {
      if (*pch == bracket)
        break;
    }
    else if (isspace(*pch) || strchr("{}()|[]<>",*pch))
      break;
  }

  setme_url.assign (start, pch-start);

  // for urls at the end of a sentence.
  if (!setme_url.empty() && strchr("?!.,", setme_url.back()))
    --setme_url.len;

  return true;
}
