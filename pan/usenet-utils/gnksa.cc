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

/*********************
**********************  Includes
*********************/

#include <config.h>

#include <cassert>
#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <ctime>
#include <vector>
#include <sstream>

extern "C"
{
  #include <ctype.h>
  #include <unistd.h>
  #include <sys/time.h>
}

#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include "gnksa.h"

/*********************
**********************  BEGINNING OF SOURCE
*********************/

using namespace pan;

#define is_code_char(A) (_code_chars[(unsigned char)(A)])
#define is_tag_char(A) (_tag_chars[(unsigned char)(A)])
#define is_unquoted_char(A) (_unquoted_chars[(unsigned char)(A)])
#define is_quoted_char(A) (_quoted_chars[(unsigned char)(A)])
#define is_paren_char(A) (_paren_chars[(unsigned char)(A)])

#if 0
#define PRINT_TABLE(A) \
	printf ("static char " #A "[UCHAR_MAX] = {"); \
	for (i=0; i<UCHAR_MAX; ++i) { \
		if (!(i%40)) \
			printf ("\n"); \
		printf ("%d,", A[i]); \
	} \
	printf ("};\n\n");

static char _unquoted_chars[UCHAR_MAX];
static char _quoted_chars[UCHAR_MAX];
static char _tag_chars[UCHAR_MAX];
static char _code_chars[UCHAR_MAX];
static char _paren_chars[UCHAR_MAX];

void
gnksa_init (void)
{
	int i;
	unsigned char ch;

  /* '!' (char)33 is allowed for message-ids
  http://tools.ietf.org/html/rfc5322#section-3.2.3
  http://tools.ietf.org/html/rfc5536#section-3.1.3
  */
	for (ch=0; ch<UCHAR_MAX; ++ch) {
		_unquoted_chars[ch] = isgraph(ch) && ch!='!' && ch!='(' && ch!=')' && ch!='<'
		                                  && ch!='>' && ch!='@' && ch!=',' && ch!=';'
		                                  && ch!=':' && ch!='\\' && ch!='"' && ch!='.'
		                                  && ch!='[' && ch!=']';
		_quoted_chars[ch] = isgraph(ch) && ch!='"' && ch!='<' && ch!='>' && ch!='\\';
		_tag_chars[ch] = isgraph(ch) && ch!='!' && ch!='(' && ch!=')' && ch!='<'
		                             && ch!='>' && ch!='@' && ch!=',' && ch!=';'
		                             && ch!=':' && ch!='\\' && ch!='"' && ch!='['
		                             && ch!=']' && ch!='/' && ch!='?' && ch!='=';
		_paren_chars[ch] = isgraph(ch) && ch!='(' && ch!=')'
		                               && ch!='<' && ch!='>'
		                               && ch!='\\';
		_code_chars[ch] = isgraph(ch) && ch!='?';
	}

	PRINT_TABLE(_unquoted_chars)
	PRINT_TABLE(_quoted_chars)
	PRINT_TABLE(_tag_chars)
	PRINT_TABLE(_code_chars)
	PRINT_TABLE(_paren_chars)
}
#else

static char _unquoted_chars[UCHAR_MAX] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,1,
0,0,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

/*
static char _unquoted_chars[UCHAR_MAX] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
0,0,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

char   : 0
char ! : 0
char " : 0
char # : 1
char $ : 1
char % : 1
char & : 1
char ' : 1
char ( : 0
char ) : 0
char * : 1
char + : 1
char , : 0
char - : 1
char . : 0

*/

#endif

static char _quoted_chars[UCHAR_MAX] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,0,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

static char _tag_chars[UCHAR_MAX] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,
0,0,1,1,0,1,1,0,1,1,1,1,1,1,1,1,1,1,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

static char _code_chars[UCHAR_MAX] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

static char _paren_chars[UCHAR_MAX] = {
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,
0,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,};

/**
***
**/

static bool
read_space (const char * start, const char ** end)
{
	/* 1*( <HT (ASCII 9)> / <blank (ASCII 32)> */
	const char * pch = start;
	while (*pch=='\t' || *pch==' ') ++pch;
	if (pch != start) {
		*end = pch;
		return true;
	}
	return false;

}

static bool
read_codes (const char * pch, const char ** end)
{
	if (!is_code_char(*pch)) return false;
	while (is_code_char(*pch)) ++pch;
	*end = pch;
	return true;
}

static bool
read_encoding (const char * pch, const char ** end)
{
	if (!is_tag_char(*pch)) return false;
	while (is_tag_char(*pch)) ++pch;
	*end = pch;
	return true;
}

static bool
read_charset (const char * pch, const char ** end)
{
	return read_encoding (pch, end);
}

static bool
read_encoded_word (const char * pch, const char ** end)
{
	/* "=?" charset "?" encoding "?" codes "?=" */
	if (pch[0]!='=' || pch[1]!='?') return false;
	pch += 2;
	if (!read_charset (pch, &pch)) return false;
	if (*pch != '?') return false;
	++pch;
	if (!read_encoding (pch, &pch)) return false;
	if (*pch != '?') return false;
	++pch;
	if (!read_codes (pch, &pch)) return false;
	if (pch[0]!='?' || pch[1]!='=') return false;
	*end = pch + 2;
	return true;
}

static bool
read_unquoted_word (const char * pch, const char ** end)
{
	/* 1*unquoted-char */
	if (!is_unquoted_char(*pch)) return false;
	while (is_unquoted_char((unsigned char)*pch)) ++pch;
	*end = pch;
	return true;
}

static bool
read_quoted_word (const char * pch, const char ** end)
{
	/* quote 1*(quoted-char / space) quote */
	if (*pch!='"') return false;
	++pch;
	for (;;) {
		if (read_space(pch,&pch))
			continue;
		if (is_quoted_char ((unsigned char)*pch)) {
			++pch;
			continue;
		}
		break;
	}
	if (*pch!='"') return false;
	++pch;
	*end = pch;
	return true;
}

static bool
read_plain_word (const char * pch, const char ** end)
{
	/* unquoted-word / quoted-word / encoded-word */
	if (read_quoted_word(pch,end)) return true;
	if (read_encoded_word(pch,end)) return true;
	if (read_unquoted_word(pch,end)) return true;
	return false;
}

static bool
read_plain_phrase (const char * pch, const char ** end)
{
	/* plain-word *(space plain-word) */
	const char * tmp = NULL;
	if (!read_plain_word(pch,&pch))
		return false;
	for (;;) {
		tmp = pch;
		if (!read_space(pch,&pch)) break;
		if (!read_plain_word(pch,&pch)) break;
	}
	*end = tmp;
	return true;
}

static bool
read_paren_char (const char * pch, const char ** end)
{
	if (!is_paren_char((unsigned char)*pch))
		return false;
	*end = pch + 1;
	return true;
}

static bool
read_paren_phrase (const char * pch, const char ** end)
{
	/* 1* (paren-char / space / encoded-word */

	if (!read_paren_char(pch,&pch)
		&& !read_space(pch,&pch)
		&& !read_encoded_word(pch,&pch))
			return false;

	for (;;)
	{
		if (!read_paren_char(pch,&pch)
			&& !read_space(pch,&pch)
			&& !read_encoded_word(pch,&pch))
				break;
	}

	*end = pch;
	return true;
}

/*****
******
*****/

/**
 *  "[244.14.124.12]"
 *  "244.12.14.12"
 */
static int
gnksa_check_domain_literal (const StringView& domain)
{
	int i;
	int x[4];
	bool need_closing_brace;
	const char * pch;

	// parse domain literal into ip number

	pch = domain.str;
	need_closing_brace = *pch == '[';
	if (need_closing_brace) ++pch; /* move past open brace */

	/* %u.%u.%u.%u */
	for (i=0; i<4; ++i) {
		char * end = NULL;
		x[i] = strtoul (pch, &end, 10);
		if (end == pch)
			return GNKSA::BAD_DOMAIN_LITERAL;
		if (x[i]<0 || x[i]>255)
			return GNKSA::BAD_DOMAIN_LITERAL;
		if (i!=3) {
			if (*end != '.')
				return GNKSA::BAD_DOMAIN_LITERAL;
			++end;
		}
		pch = end;
	}

	if (need_closing_brace && *pch!=']')
		return GNKSA::BAD_DOMAIN_LITERAL;

	return GNKSA::OK;
}

/*****
******
*****/

int
GNKSA :: check_domain (const StringView& domain)
{
   if (domain.empty())
      return GNKSA::ZERO_LENGTH_LABEL;

   if (*domain.str == '[')
      return gnksa_check_domain_literal (domain);

   if (*domain.str=='.'
      || domain.str[domain.len-1]=='.'
      || domain.strstr("..")!=NULL)
      return GNKSA::ZERO_LENGTH_LABEL;

   // count the labels
   int label_qty = 0;
   StringView token, mydomain(domain);
   while (mydomain.pop_token (token, '.'))
      ++label_qty;

   // make sure we have more than one label in the domain
   if (label_qty < 2)
      return GNKSA::SINGLE_DOMAIN;

   // check for illegal labels
   mydomain = domain;
   for (int i=0; i<label_qty-1; ++i) {
      mydomain.pop_token (token, '.');
      if (token.len > 63)
         return GNKSA::ILLEGAL_LABEL_LENGTH;
      if (token.str[0]=='-' || token.str[token.len-1]=='-')
         return GNKSA::ILLEGAL_LABEL_HYPHEN;
   }

   // last label -- toplevel domain
   mydomain.pop_token (token, '.');
   switch (token.len)
   {
      case 1:
         if (isdigit((unsigned char)*token.str))
            return gnksa_check_domain_literal (domain);
         // single-letter TLDs dont exist
         return GNKSA::ILLEGAL_DOMAIN;

      case 2:
         if (isdigit((unsigned char)token.str[0]) || isdigit((unsigned char)token.str[1]))
            return gnksa_check_domain_literal (domain);
         break;

      case 3:
         if (isdigit((unsigned char)token.str[0]) ||
             isdigit((unsigned char)token.str[2]) ||
             isdigit((unsigned char)token.str[3]))
            return gnksa_check_domain_literal (domain);
         break;

      default:
         break;
   }

   return GNKSA::OK;
}

/*****
******
*****/

namespace
{
  int
  check_localpart (const StringView& localpart)
  {
    // make sure it's not empty...
    if (localpart.empty())
      return GNKSA::LOCALPART_MISSING;

    // break localpart up into its unquoted words
    StringView token, mylocal(localpart);
    while (mylocal.pop_token (token, '.')) {
      if (token.empty())
        return GNKSA::ZERO_LENGTH_LOCAL_WORD;
      foreach_const (StringView, token, it)
        if (!is_unquoted_char(*it))
          return GNKSA::INVALID_LOCALPART;
    }

    return GNKSA::OK;
  }

   int
   check_address (const StringView& address)
   {
      int retval = GNKSA::OK;

      // get rid of vacuous case
      if (address.empty())
         return GNKSA::LOCALPART_MISSING;

      // check the address
      const char * pch = address.strrchr ('@');
      if (pch == NULL)
         retval = GNKSA::INVALID_DOMAIN;
      else {
         StringView username, domain;
         address.substr (NULL, pch, username);
         address.substr (pch+1, NULL, domain);
         if (retval == GNKSA::OK)
            retval = GNKSA :: check_domain (domain);
         if (retval == GNKSA::OK)
            retval = check_localpart (username);
      }

      return retval;
   }
};

/*****
******
*****/

namespace
{
   enum { ADDRTYPE_ROUTE, ADDRTYPE_OLDSTYLE };

   int split_from (const StringView   & from,
                   StringView         & addr,
                   StringView         & name,
                   int                & addrtype,
                   bool                 strict)
   {
      char * lparen;

      addr.clear ();
      name.clear ();

      StringView myfrom(from);
      myfrom.trim ();

      // empty string
      if (myfrom.empty()) {
         addrtype = ADDRTYPE_OLDSTYLE;
         return GNKSA::LPAREN_MISSING;
      }

      if (myfrom.back() == '>') // Charles Kerr <charles@rebelbase.com>
      {
         addrtype = ADDRTYPE_ROUTE;

         // get address part
         char * begin = myfrom.strrchr ('<');
         if (!begin)
            return GNKSA::LANGLE_MISSING;

         // copy route address from inside the <> brackes
         StringView myaddr (myfrom.substr (begin+1, NULL));
         const char * gt = myaddr.strchr ('>');
         if (gt != NULL)
            myaddr = myaddr.substr (NULL, gt);
         addr = myaddr;

         if (strict) {
            const char * tmp = myfrom.str;
            if ((*tmp) && (!read_plain_phrase(tmp,&tmp) || !read_space(tmp,&tmp)))
               return GNKSA::ILLEGAL_PLAIN_PHRASE;
         }

         // get realname part
         StringView myname (myfrom.substr (NULL, begin));
         myname.trim ();
         name = myname;
      }
      else if ((lparen = myfrom.strchr('(')) != NULL) // charles@rebelbase.com (Charles Kerr)
      {
         addrtype = ADDRTYPE_OLDSTYLE;

         // address part
         StringView myaddr (myfrom.substr (nullptr, lparen));
         myaddr.trim ();
         addr = myaddr;
         if (strict) {
            const int val (check_address (addr));
            if (val)
               return val;
         }

         // real name part
         StringView myname (myfrom.substr (lparen+1, nullptr));
         myname.trim ();
         if (myname.back() != ')')
            return GNKSA::RPAREN_MISSING;

         myname = myname.substr (NULL, myname.end()-1);
         const char * end = nullptr;
         if (strict && (!read_paren_phrase(myname.str,&end) || end==NULL))
            return GNKSA::ILLEGAL_PAREN_PHRASE;

         name = myname;
      }
      else if (myfrom.strchr('@') != NULL) /* charles@rebelbase.com */
      {
         addr = myfrom;
         return strict ? check_address(addr) : GNKSA::OK;
      }
      else // who knows what this thing is...
      {
         name = myfrom;
         return GNKSA::LPAREN_MISSING;
      }

      return GNKSA::OK;
   }
};


/*****
******
*****/

/************
*************  PUBLIC
************/

/*****
******
*****/












































int
GNKSA :: do_check_from (const StringView   & from,
                        StringView         & addr,
                        StringView         & name,
	                bool                 strict)
{
   int addrtype = 0;

   // split from
   addr.clear ();
   name.clear ();
   int retval = split_from (from, addr, name, addrtype, strict);

   // check address
   if (retval==OK && !addr.empty())
      retval = check_address (addr);

   return retval;
}

StringView
GNKSA :: get_short_author_name (const StringView& author)
{
  StringView addr, name;
  GNKSA::do_check_from (author, addr, name, false);

  // if we have just one of (name, addr) then
  // there was probably a problem parsing...
  // try to trim out the address by looking for '@'

  StringView s (author);
       if (name.empty() && !addr.empty()) StringView(addr).pop_token(s,'@');
  else if (addr.empty() && !name.empty()) StringView(name).pop_token(s,'@');
  else if (!name.empty()) s = name;
  else if (!addr.empty()) s = addr;

  s.trim ();
  if (s.len>2 && s.front()=='"' && s.back()=='"')
    s = s.substr(s.begin()+1, s.end()-1);

  return s;
}


/*****
******
*****/

int
GNKSA :: check_from (const StringView& from, bool strict)
{
  StringView addr, name;
  return do_check_from (from, addr, name, strict);
}

/*****
******
*****/

namespace
{
   /**
    * Validates a Message-ID from a References: header.
    *
    * The purpose of this function is to remove severely broken,
    * usually suntactically-invalid Message-IDs, such as those
    * missing '<', '@', or '>'.
    *
    * However we want to retain Message-IDs that might be sloppy,
    * such as ones that have possibly-invalid domains.
    *
    * This balance is from wanting to adhere to GNKSA that wants us
    * to remove `damaged' Message-IDs, but we want to be pretty
    * forgiving because these References are required for threading
    * to work properly.
    */
   int
   check_message_id (const StringView& message_id)
   {
      StringView tmp (message_id);
      tmp.trim ();

      // make sure it's <= 250 octets (son of gnksa 1036 5.3)
      if (tmp.len > 250)
         return GNKSA::ILLEGAL_LABEL_LENGTH;

      // make sure the message-id is wrapped in < >
      if (tmp.str[0]!='<')
         return GNKSA::LANGLE_MISSING;

      if (tmp.str[tmp.len-1]!='>')
         return GNKSA::RANGLE_MISSING;

      // find the '@' separator
      char * pch = tmp.strrchr ('@');
      if (pch == NULL)
         return GNKSA::ATSIGN_MISSING;

      // check the domain name
      StringView domain (tmp.substr (pch+1, NULL));
      --domain.len; // remove trailing '>'
      if (domain.empty())
         return GNKSA::ILLEGAL_DOMAIN;

      // check the local-part
      StringView local (tmp.substr (tmp.str+1, pch));
      if (local == "postmaster")
         return GNKSA::INVALID_LOCALPART;

      return check_localpart (local);
   }
};

/*****
******
*****/

std::string
GNKSA :: remove_broken_message_ids_from_references (const StringView& references)
{
  std::string s;

  // remove broken message-ids
  StringView v (references);
  while (!v.empty())
  {
    // find the beginning of the message-id
    const char * begin = v.strchr ('<');
    if (!begin)
      break;

    // find the end of the message-id
    v = v.substr (begin+1, nullptr);
    const char * end = v.strpbrk ("<> ");
    if (!end)
      end = v.end();
    else if (*end == '>')
      ++end;
    v = v.substr (end, nullptr);

    // check the message-id for validity
    if (check_message_id (StringView(begin, end-begin)) == GNKSA::OK) {
      s.insert (s.end(), begin, end);
      s += ' ';
    }
  }

  if (!s.empty())
    s.erase (s.end()-1); // remove trailing space

  return s;
}

/**
 * Try to trim references down to an acceptable length for the NNTP server,
 * @param untrimmed "references" string
 * @return newly-allocated trimmed "references" string
 */
std::string
GNKSA :: trim_references (const StringView& refs, size_t cutoff)
{
   std::string fixed (remove_broken_message_ids_from_references (refs));

   StringView left, unused, myrefs(fixed);
   myrefs.pop_token (left, ' ');
   const size_t len_left = cutoff - left.len - 1; // 1 for ' ' between left and right
   while (myrefs.len > len_left)
      myrefs.pop_token (unused, ' ');

   std::string s;
   s.insert (s.end(), left.begin(), left.end());
   if (!myrefs.empty())
     s += ' ';
   s.insert (s.end(), myrefs.begin(), myrefs.end());
   assert (s.size() <= cutoff);
   return s;
}

/**
***
**/

static const char* default_domain = "nospam.com";
static MTRand rng;

/**
 * thus spake son-of-1036: "the most popular method of generating local parts
 * is to use the date and time, plus some way of distinguishing between
 * simultaneous postings on the same host (e.g., a process number), and encode
 * them in a suitably-reduced alphabet.
 */
std::string
GNKSA :: generate_message_id (const StringView& domain)
{
   static bool rand_inited (false);
   if (!rand_inited)
   {
     rng.seed();
     rand_inited = true;
   }

   std::stringstream out;
   struct timeval tv;
   out << "pan$";
   gettimeofday (&tv, NULL);
   out << std::hex << tv.tv_usec << "$" << std::hex
       << rng.randInt() << "$" << std::hex << rng.randInt() << "$"
       << std::hex << rng.randInt();
   out << '@' << (domain.empty() ? default_domain : domain);
   return out.str();
}

std::string
GNKSA :: generate_message_id_from_email_address (const StringView& addr)
{
   StringView domain;

   // find the domain in the email address
   if (!addr.empty()) {
      const char * pch = addr.strchr ('@');
      if (pch != NULL)
         domain = addr.substr (pch+1, NULL);
      else
         domain = addr;
   }

   // fallback to default domain
   if (domain.empty()) {
      Log::add_info_va (_("No email address provided; generating message-id with domain \"%s\""), default_domain);
      domain = default_domain;
   }

   // strip out closing bracket
   const char * pch = domain.strchr ('>');
   if (pch != NULL)
      domain = domain.substr (NULL, pch);
   domain.trim ();

   return generate_message_id (domain);
}

std::string
GNKSA :: generate_references (const StringView   & references,
                              const StringView   & message_id)
{
   std::string s;

   if (!message_id.empty()) {
      if (!references.empty()) {
         s.insert (s.end(), references.begin(), references.end());
         s += ' ';
      }
      s.insert (s.end(), message_id.begin(), message_id.end());
      s = trim_references (s);
   }

   return s;
}

/***
****  Signatures
***/

GNKSA::SigType
GNKSA::is_signature_delimiter (const StringView& line)
{
   switch (line.len) {
      case 2: if (!strncmp (line.str,"--"   ,2)) return SIG_NONSTANDARD;
         break;
      case 3: if (!strncmp (line.str,"--\r" ,3)) return SIG_NONSTANDARD;
         if (!strncmp (line.str,"-- "  ,3)) return SIG_STANDARD;
         break;
      case 4: if (!strncmp (line.str,"-- \r",4)) return SIG_STANDARD;
      default:
         return SIG_NONE;
   }
   return SIG_NONE;
}


GNKSA::SigType
GNKSA::find_signature_delimiter (const StringView& body,
                                 int& setme_pos)
{
   const static int SIG_THRESHOLD = 6;
   int sig_pos = -1;
   int sig_type = SIG_NONE;
   int lines_below = 0;

   setme_pos = -1;

   // iterate through the text, line by line
   StringView line, mybody(body);
   while (mybody.pop_token (line, '\n'))
   {
      const SigType st (is_signature_delimiter (line));
      if (st != SIG_NONE) {
         sig_type = st;
         sig_pos = line.str - body.str;
         lines_below = 0;
      } else if (sig_pos != -1) {
         ++lines_below;
      }
   }

   if (sig_type == SIG_NONE)
      return SIG_NONE;

   if (sig_pos == -1)
      return SIG_NONE;

   if (sig_type == SIG_STANDARD) {
      setme_pos = sig_pos;
      return SIG_STANDARD;
   }

   // if we have a non-standard sig, make sure it's the last one
   // and that there are less than SIG_THRESHOLD lines
   if (sig_type == SIG_NONSTANDARD && lines_below <= SIG_THRESHOLD ) {
      setme_pos = sig_pos;
      return SIG_NONSTANDARD;
   }

   return SIG_NONE;
}


bool
GNKSA :: remove_signature (StringView& body)
{
   int sig_point (-1);
   const bool has_signature (find_signature_delimiter (body, sig_point) != SIG_NONE);
   if (has_signature)
      body = body.substr (nullptr, body.str+sig_point);
   return has_signature;
}
