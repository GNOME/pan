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

#ifndef __StringView_h__
#define __StringView_h__

#include <iosfwd>
#include <string>
#include <cstring>
#include <glib.h>

namespace pan
{
   /**
    * A shallow copy a C string, plus utilities to let us
    * substring, tokenize, walk, search, or otherwise
    * manipulate it without having to modify the original or
    * allocate new strings.
    *
    * @ingroup general
    */
   struct StringView
   {
      public:

         static int strcmp (const char * str_a,
                            size_t       str_a_len,
                            const char * str_b,
                            size_t       str_b_len);

         static char* strchr (const char * haystack,
                              size_t       haystack_len,
                              char         needle)
           { return (char*) memchr (haystack, needle, haystack_len); }

         static char* strrchr (const char * haystack,
                               size_t       haystack_len,
                               char         needle);

         static int strncpy (char        * target,
                             size_t        target_size,
                             const char  * source_str,
                             size_t        source_len);

         static char* strstr (const char * haystack,
                              size_t       haystack_len,
                              const char * needle,
                              size_t       needle_len);

         static char* strpbrk (const char * haystack,
                               size_t       haystack_len,
                               const char * needles);

      public:

         const char * str;
         size_t len;

         typedef const char* const_iterator;
         const_iterator begin() const { return str; }
         const_iterator end() const { return str+len; }
         const char& front() const { return str[0]; }
         const char& back() const { return str[len-1]; }

      public:

         StringView (): str(nullptr), len(0) {}
         StringView (const std::string& s) { assign(s); }
         StringView (const char * s) { assign(s); }
         StringView (const char * s, size_t l) { assign(s,l); }
         StringView (const char * s, const char * e) { assign(s,e-s); }
         StringView (const StringView& p): str(p.str), len(p.len) {}
         StringView& operator=(StringView const &) = default;
         ~StringView () { str = (char*)0xDEADBEEF; len = (size_t)~0; }

      public:

         bool empty () const { return !len || !str || !*str; }

         std::string to_string () const {return empty()
                                         ? std::string()
                                         : std::string(str,str+len); }
         operator std::string () const { return to_string(); }

         StringView substr (const char * start, const char * end) const;
         void substr (const char * start, const char * end, StringView& setme) const;
         void eat_chars (size_t);
         void truncate (size_t);
         void rtruncate (size_t);

         bool operator== (const StringView& p) const { return !strcmp(p); }
         bool operator!= (const StringView& p) const { return !(*this == p); }
         bool operator<  (const StringView& p) const { return strcmp(p)<0; }

         bool operator== (const char *s) const { return !strcmp(str,len,s,strlen(s)); }
         bool operator!= (const char *s) const { return  strcmp(str,len,s,strlen(s)); }

         int strcmp (const StringView& p) const {
            return strcmp (str, len, p.str, p.len); }

         int strncasecmp (const char * p, unsigned int l) const {
           if (len >= l)
             return g_ascii_strncasecmp (str, p, l);
           else {
             int i = g_ascii_strncasecmp (str, p, len);
             if (i)
               return i;
             return -1; // shorter, so less than p
           }
         }

         int strcmp (const char * s, size_t l) const {
            return strcmp (str, len, s, l); }

         char* strchr (char needle, size_t p=0) const {
            return p<len ? strchr (str+p, len-p, needle) : nullptr; }

         char* strrchr (char needle) const {
            return strrchr (str, len, needle); }

         char* strstr (const StringView& p) const {
            return strstr (str, len, p.str, p.len); }

         char* strstr (const char* s) const {
            return strstr (str, len, s, ::strlen(s)); }

         char* strpbrk (const char * needles) const {
            return strpbrk (str, len, needles);
         }

      public:

         void assign           (const std::string& s) {str=s.c_str(); len=s.size(); }
         StringView& operator= (const std::string& s) {str=s.c_str(); len=s.size(); return *this;}
         void assign           (const char * s) { str=s; len=s?strlen(s):0; }
         StringView& operator= (const char * s) { str=s; len=s?strlen(s):0; return *this; }
         void assign (const char * s, size_t l) { str=s; len=l; }

         void clear () { str=nullptr; len=0; }

         void trim ();
         void ltrim ();
         void rtrim ();

         bool pop_token (StringView& setme, char delimiter=' ');
         bool pop_last_token (StringView& setme, char delimiter=' ');
   };

   std::ostream& operator<< (std::ostream& os, const StringView& s);
};

#endif /* __StringView_h__ */
