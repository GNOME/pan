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

extern "C" {
  #include <ctype.h>
}
#include <glib.h>
#include <glib/gi18n.h>
#include "debug.h"
#include "log.h"
#include "text-match.h"

using namespace pan;

/*****
******
******  Regex Munging
******
*****/

#define is_metacharacter(A) (_metacharacters[(guchar)(A)])
#if 0
static char _metacharacters[UCHAR_MAX];
#define PRINT_TABLE(A) \
	printf ("static char " #A "[UCHAR_MAX] = {"); \
	for (i=0; i<UCHAR_MAX; ++i) { \
		if (!(i%40)) \
			printf ("\n\t"); \
		printf ("%d,", A[i]); \
	} \
	printf ("};\n\n");
static void
build_table (void)
{
	int i;
	unsigned char ch;

	for (ch=0; ch<=UCHAR_MAX; ++ch) {
		_metacharacters[ch] = ch=='.' || ch=='^' || ch=='$' || ch=='*' ||
		                      ch=='+' || ch=='?' || ch=='{' || ch=='[' ||
				      ch=='|' || ch=='(' || ch==')' || ch=='\\';
	}

	PRINT_TABLE(_metacharacters)
}
#else
static char _metacharacters[UCHAR_MAX+1] = {
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,
	1,1,1,1,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
#endif

namespace
{
   char*
   regexp_unescape (const char * in)
   {
      char * retval = g_new (char, strlen(in)+1);
      char * out = retval;

      if (*in == '^')
         ++in;
      while (*in) {
         if (in[0]=='\\' && is_metacharacter(in[1]))
            ++in;
         *out++ = *in++;
      }

      if ((out-retval>1) && in[-1]=='$' && in[-2]!='\\')
         --out;
      *out = '\0';
      return retval;
   }

   std::string
   quote_regexp (const StringView& in)
   {
      std::string s;
      for (const char *pch=in.begin(), *end=in.end(); pch!=end; ++pch) {
         if (is_metacharacter (*pch))
            s += '\\';
         s += *pch;
      }
      return s;
   }

   /**
    * Try to downgrade the more-expensive regexes to a cheaper type.
    */
   TextMatch::Type
   get_real_match_type (const StringView& key, TextMatch::Type type)
   {
      bool starts_with (false);
      bool ends_with (false);

      if (key.empty())
         return type;

      // if it's not a regex, keep it
      if (type != TextMatch::REGEX)
         return type;

      // must it be a regex?
      for (const char * front=key.begin(), *pch=front, *end=key.end(); pch!=end; ++pch)
         if (*pch=='\\' && is_metacharacter(pch[1]))
            ++pch;
         else if (*pch=='^' && pch==front)
            starts_with = true;
         else if (*pch=='$' && pch+1==end)
            ends_with = true;
         else if (is_metacharacter(*pch))
            return TextMatch::REGEX;

      if (starts_with && ends_with)
         return TextMatch::IS;

      if (starts_with)
         return TextMatch::BEGINS_WITH;

      if (ends_with)
         return TextMatch::ENDS_WITH;

      return TextMatch::CONTAINS;
   }
}

/*****
******
******  Regex Testing
******
*****/

/**
 * Private internal TextMatch class for regular expression testing.
 * @see TextMatch
 * @ingroup general
 */
class pan::TextMatch::PcreInfo
{
   public:

      GRegex * re;

   public:

      PcreInfo (): re(nullptr) { }




      ~PcreInfo ()
      {
         if (re)
            g_regex_unref (re);
       }

    public:

       bool set (const std::string&  pattern,
                 bool                case_sensitive)
       {
          GRegexCompileFlags options;

          if (case_sensitive)
            options = (GRegexCompileFlags)0;
          else
            options = (GRegexCompileFlags)G_REGEX_CASELESS;

          GError * err = nullptr;
          re = g_regex_new (pattern.c_str(), options, (GRegexMatchFlags)0, &err);
          if (err) {
            Log::add_err_va (_("Can't use regular expression \"%s\": %s"), pattern.c_str(), err->message);
            g_error_free (err);
            return false;
          }

          return true;
       }
};

int
TextMatch :: my_regexec (const StringView& text) const
{
   if (_pcre_state == NEED_COMPILE)
   {
      _pcre_info = new PcreInfo ();

      if (_pcre_info->set (_impl_text, state.case_sensitive))
         _pcre_state = COMPILED;
      else
         _pcre_state = ERR;
   }

   return _pcre_state != COMPILED
      ? -1
      : g_regex_match_full (_pcre_info->re,
                            text.str,
                            text.len,
                            0,
                            G_REGEX_MATCH_NOTEMPTY,
                            NULL,
                            NULL);
}

/*****
******
******  Substring Testing
******
*****/

/**
 * Boyer-Moore-Horspool-Sunday search algorithm.
 * case-sensitive and insensitive versions.
 */
namespace
{
   int
   bmhs_isearch (const StringView& text_in,
                 const StringView& pat_in,
                 const char * skip)

   {
      const guchar * text = (const guchar*) text_in.str;
      const size_t text_len = text_in.len;
      const guchar * pat = (const guchar*) pat_in.str;
      const size_t pat_len = pat_in.len;
      const guchar first_uc = toupper(*pat);
      const guchar first_lc = tolower(*pat);
      const guchar * t = text;
      const guchar * text_end = text + text_len - pat_len + 1;
      const guchar * pat_end = pat + pat_len;
      const guchar * p;
      const guchar * q;

      for (;;)
      {
         // scan loop that searches for the first character of the pattern
         while (t<text_end && *t!=first_uc && *t!=first_lc)
            t += std::max (1, (int)skip[tolower(t[pat_len])]);
         if (t >= text_end)
            break;

         // first character matches, so execute match loop in fwd direction
         p = pat;
         q = t;
         while (++p < pat_end && *p == tolower(*++q))
            ;

         if (p == pat_end)
            return t - text;

         t += skip[t[pat_len]];
      }

      return -1;
   }

   /**
    * Boyer-Moore-Horspool-Sunday search algorithm.
    * Returns position of match, or -1 if no match.
    */
   static int
   bmhs_search (const StringView& text_in,
                const StringView& pat_in,
                const char * skip)

   {
      const guchar * text = (const guchar*) text_in.str;
      const size_t text_len = text_in.len;
      const guchar * pat = (const guchar*) pat_in.str;
      const size_t pat_len  = pat_in.len;
      const guchar first = *pat;
      const guchar * t = text;
      const guchar * text_end = text + text_len - pat_len + 1;
      const guchar * pat_end  = pat + pat_len;
      const guchar * p;
      const guchar * q;

      for (;;)
      {
         // scan loop that searches for the first character of the pattern
         while (t<text_end && *t!=first)
            t += skip[t[pat_len]];
         if (t >= text_end)
            break;

         // first character matches, so execute match loop in fwd direction
         p = pat;
         q = t;
         while (++p < pat_end && *p == *++q)
            ;

         if (p == pat_end)
            return t - text;

         t += skip[t[pat_len]];
      }

      return -1;
   }
};

/*****
******
******
******
*****/

bool
TextMatch :: test (const StringView& text_in) const
{
   bool retval (false);
   StringView text (text_in);

   if (!text.empty())
   {
      const StringView pat (_impl_text);

      switch (_impl_type)
      {
         case REGEX:
            //std::cerr << LINE_ID << " regex..." << std::endl;
            retval = my_regexec (text) > 0;
            break;

         case ENDS_WITH:
            //std::cerr << LINE_ID << " ends with..." << std::endl;
            if (text.len < pat.len)
               retval = false;
            text.rtruncate (pat.len);
            // fall through to "is"

         case IS:
            //std::cerr << LINE_ID << " is..." << std::endl;
            if (state.case_sensitive)
               retval = text.len==pat.len && !StringView::strcmp (text.str, text.len, pat.str, pat.len);
            else
               retval = text.len==pat.len && !g_ascii_strncasecmp (text.str, pat.str, pat.len);
            break;

         case BEGINS_WITH:
            //std::cerr << LINE_ID << " begins with..." << std::endl;
            if (state.case_sensitive)
               retval = text.len>=pat.len && !strncmp (text.str, pat.str, pat.len);
            else
               retval = text.len>=pat.len && !g_ascii_strncasecmp (text.str, pat.str, pat.len);
            break;

         case CONTAINS:
            //std::cerr << LINE_ID << " contains..." << std::endl;
            if (state.case_sensitive)
               retval = text.len>=pat.len && bmhs_search (text, pat, _skip) != -1;
            else
               retval = text.len>=pat.len && bmhs_isearch (text, pat, _skip) != -1;
            break;
      }
   }

   return state.negate ? !retval : retval;
}

/*****
******
******
******
*****/

TextMatch :: TextMatch ():
   _impl_text (),
   _skip (new char [UCHAR_MAX+1]),
   _pcre_info (nullptr),
   _pcre_state (NEED_COMPILE)
{
}

TextMatch :: ~TextMatch ()
{
   clear ();
   delete [] _skip;
}

void
TextMatch :: clear ()
{
  state.text.clear ();
  _impl_text.clear ();

  if (_pcre_state == COMPILED)
  {
    delete _pcre_info;
    _pcre_info = nullptr;
    _pcre_state = NEED_COMPILE;
  }
}

void
TextMatch :: set (const StringView  & text,
                  Type                type,
                  bool                case_sensitive,
                  bool                negate)
{
  clear ();

  state.text.clear ();
  state.text.insert (0, text.str, text.len);
  state.type = type;
  state.negate = negate;
  state.case_sensitive = case_sensitive;

  _impl_type = get_real_match_type (text, type);

  if (state.type == _impl_type)
    _impl_text = state.text;
  else {
    char * tmp = regexp_unescape (state.text.c_str());
    _impl_text = tmp;
    g_free (tmp);
  }

  if (!state.case_sensitive) {
    char * pch = g_utf8_strdown (_impl_text.c_str(), -1);
    _impl_text = pch;
    g_free (pch);
  }

//std::cerr << LINE_ID << " state.type " << state.type << " _impl_type " << _impl_type << " text " << state.text << " _impl_text " << _impl_text << std::endl;

  // Boyer-Moore-Horspool-Sunday
  const char * pat (_impl_text.c_str());
  const unsigned int len (_impl_text.size());
  for (int i=0; i<=UCHAR_MAX; ++i)
    _skip[i] = len + 1;
  for (unsigned int i=0; i<len; i++)
    _skip[(guchar)(pat[i])] = len - i;
}

/**
***
**/


/*static*/ std::string
TextMatch :: create_regex (const StringView  & in,
                           Type                type)
{
   std::string s;

   if (type == REGEX)
      s.insert (s.end(), in.begin(), in.end());
   else
      s = quote_regexp (in);

   StringView tmp (s);
   tmp.trim ();
   s = tmp;

   if (!s.empty())
   {
      if (type == IS || type == BEGINS_WITH)
         s.insert (s.begin(), '^');
      if (type == IS || type == ENDS_WITH)
         s.insert (s.end(), '$');
   }

   return s;
}

/*static*/ bool
TextMatch :: validate_regex (const char * text)
{
   PcreInfo i;
   bool ok (i.set (text, false));
   return ok;
}

TextMatch :: TextMatch (const TextMatch& that):
  _skip (new char [UCHAR_MAX+1]),
  _pcre_info (nullptr),
  _pcre_state (NEED_COMPILE)
{
  set (that.state);
}

//FIXME this is clearly doing something odd, but the solution of deleting the
//assignment and writing defaulted move constructors/assignments won't work
//till C++20
TextMatch&
TextMatch :: operator= (const TextMatch& that)
{
  set (that.state);
  return *this;
}

