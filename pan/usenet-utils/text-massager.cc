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
#include <vector>
#include <map>
#include <cstring>
#include <glib.h>
#include <glib/gi18n.h>
#include "text-massager.h"
#include <pan/general/log.h>
#include <pan/general/e-util.h>

using namespace pan;

TextMassager :: TextMassager ():
   _wrap_column (74)
{
   _quote_characters = new char [UCHAR_MAX];
   memset (_quote_characters, 0, UCHAR_MAX);
   _quote_characters[(int)'>'] = true;
}

TextMassager :: ~TextMassager ()
{
   delete [] _quote_characters;
}

/****
*****  QUOTE CHARACTERS
****/

bool
TextMassager :: is_quote_character (unsigned int unichar) const
{
  return unichar > UCHAR_MAX ? false : _quote_characters[unichar];
}

std::set<char>
TextMassager :: get_quote_characters () const
{
   std::set<char> retval;

   for (int i=0; i<UCHAR_MAX; ++i)
      if (_quote_characters[i])
         retval.insert (i);

    return retval;
}

void
TextMassager::set_quote_characters (const std::set<char>& chars)
{
   for (int i=0; i<UCHAR_MAX; ++i)
      _quote_characters[i] = false;

   typedef std::set<char>::const_iterator c_cit;
   for (c_cit it=chars.begin(), end=chars.end(); it!=end; ++it)
      _quote_characters[(int)*it] = true;
}

/****
*****  LINE WRAPPING
****/

namespace
{
   struct Line
   {
      public:
         StringView leader;
         StringView content;
   };

   typedef std::vector<Line> lines_t;
   typedef lines_t::const_iterator lines_cit;

   struct Paragraph
   {
      public:
         std::string leader;
         std::string content;
      public:
         Paragraph () { }
         Paragraph (const char * l, int llen,
                    const char * c, int clen):
            leader (l, llen),
            content (c, clen) { }
   };

   typedef std::vector<Paragraph> paragraphs_t;
   typedef paragraphs_t::iterator p_it;

   void merge_fixed (paragraphs_t &paragraphs, lines_t &lines, int wrap_col)
   {
     std::map<std::string, int> max_map;
     std::map<std::string, int>::iterator map_end = max_map.end();
     int prev_content_len = 0;
     StringView cur_leader;
     std::string cur_content;
     bool prev_ends_with_punct = true;

     for (lines_cit it=lines.begin(), end=lines.end(); it!=end; ++it)
     {
       const Line& line (*it);
       int line_len = line.leader.len + line.content.len;
       if (map_end != max_map.find(line.leader))
         max_map[line.leader] = MAX(max_map[line.leader], line_len );
       else
         // use 12 as the minimum line length
         max_map.insert( std::make_pair(line.leader, MAX(line_len, 12) ) );
     }

     for (lines_cit it=lines.begin(), end=lines.end(); it!=end; ++it)
     {
        const Line& line (*it);
        bool paragraph_end = true;
        bool hard_break = false;

        if (cur_content.empty() || line.leader==cur_leader)
           paragraph_end = false;

        if (line.content.empty()) {
           hard_break = prev_content_len!=0;
           paragraph_end = true;
        }

        // if first word could have been wrapped onto previous line
        // line but wasn't assume deliberate line break.
        if (!paragraph_end && prev_content_len && line.content.len)
        {
          unsigned int space = max_map[line.leader] - (prev_content_len + line.leader.len) - 1;
          if ( space > 0 && ((line.content.len < space)
                              || g_utf8_strchr (line.content.str, space, ' ')) )
            //paragraph_end = true;
            paragraph_end = prev_ends_with_punct;
        }

        if (paragraph_end) // the new line is a new paragraph, so save old
        {
           paragraphs.push_back (Paragraph (
              cur_leader.str, cur_leader.len,
              cur_content.c_str(), cur_content.size()));
           cur_leader = line.leader;
           cur_content = line.content.to_string();
           if (hard_break) {
              paragraphs.push_back (Paragraph (
                 cur_leader.str, cur_leader.len, "", 0));
            }
        }
        else // append to the content
        {
           if (!cur_content.empty())
              cur_content += ' ';
           cur_leader = line.leader;
           cur_content.insert (cur_content.end(),
                               line.content.begin(),
                               line.content.end());
        }

        prev_content_len = line.content.len;
        if ( prev_content_len > 1)
          prev_ends_with_punct = g_unichar_ispunct ( g_utf8_get_char (
            g_utf8_find_prev_char ( line.content.str, line.content.str + line.content.len )));
        else
          prev_ends_with_punct = true;
     }
   }

  void merge_flowed (paragraphs_t &paragraphs, lines_t &lines)
  {
    StringView cur_leader;
    std::string cur_content;
    bool prev_flowed=true;

    for (lines_cit it=lines.begin(), end=lines.end(); it!=end; ++it)
    {
      const Line& line (*it);

      if (line.leader != cur_leader)
        prev_flowed = false;

      if (!prev_flowed) // the new line is a new paragraph, so save old
      {
         paragraphs.push_back (Paragraph (
            cur_leader.str, cur_leader.len,
            cur_content.c_str(), cur_content.size()));
         cur_leader = line.leader;
         cur_content = line.content.to_string();
      }
      else // append to the content
      {
         if (!cur_content.empty())
            cur_content += ' ';
         cur_leader = line.leader;
         cur_content.insert (cur_content.end(),
                             line.content.begin(),
                             line.content.end());
      }
      // flowed text ends with a space
      if (line.content.len && line.content.str[line.content.len] == ' ')
         prev_flowed = true;
      else
        prev_flowed = false;
    }
    //add final content
    paragraphs.push_back (Paragraph (cur_leader.str, cur_leader.len,
                            cur_content.c_str(), cur_content.size()));
 }

   std::vector<Paragraph>
   get_paragraphs (const TextMassager& tm, const StringView& body, bool flowed)
   {
      StringView mybody (body);
      StringView line;
      lines_t lines;
      while (mybody.pop_token (line, '\n'))
      {
         const char * pch = line.str;
         const char * end = line.str + line.len;

         while (pch<end &&
               (tm.is_quote_character (g_utf8_get_char(pch))
               || g_unichar_isspace(g_utf8_get_char(pch))))
            pch=g_utf8_next_char(pch);

         Line l;
         l.leader.assign (line.str, pch-line.str);
         l.content.assign (pch, end-pch);
         l.content.trim ();
         lines.push_back (l);
      }

      // add an empty line to make the paragraph-making loop go smoothly
      Line l;
      l.leader.clear ();
      l.content.clear ();
      lines.push_back (l);

      // merge the lines into paragraphs
      std::vector<Paragraph> paragraphs;
      if (!lines.empty())
      {
        if (flowed)
          merge_flowed(paragraphs, lines);
        else
          merge_fixed(paragraphs, lines, tm.get_wrap_column());

        // Remember that empty line we added back up at the top?
        // We remove it now
        if (!paragraphs.empty())
           paragraphs.resize (paragraphs.size()-1);
      }

      return paragraphs;
   }

   void
   wrap_line_at_column (char    * str,
                        int       len,
                        int       column)
   {
      int pos = 0;
      int space_len;
      char * linefeed_here = NULL;

      // walk through the entire string
      for (char *pch=str, *end=pch+len; pch!=end; )
      {
         // a linefeed could go here; remember this space
         gunichar ch = g_utf8_get_char (pch);
         if (g_unichar_isspace ( ch ) || *pch=='\n')
           if (g_unichar_break_type(ch) != G_UNICODE_BREAK_NON_BREAKING_GLUE)
           {
             linefeed_here = pch;
             // not all spaces are single char
             space_len = g_utf8_next_char (pch) - pch;
           }

         // line's too long; add a linefeed if we can
         if (pos>=column && linefeed_here!=NULL)
         {
            const char nl[5]="   \n";
            if( space_len == 1)
              *linefeed_here = '\n';
             else
               memcpy( linefeed_here, 4 - space_len + nl, space_len);
            pch = linefeed_here + space_len;
            linefeed_here = NULL;
            space_len = 0;
            pos = 0;
         }
         else
         {
            pch = g_utf8_next_char (pch);
            ++pos;
         }
      }
   }

   void
   add_line (std::vector<std::string>  & setme,
             const StringView          & leader,
             const StringView          & content)
   {
      std::string s;
      s.insert (s.end(), leader.begin(), leader.end());
      s.insert (s.end(), content.begin(), content.end());
      setme.push_back (s);
   }

   void
   fill_paragraph (const TextMassager        & tm,
                   Paragraph                 & p,
                   std::vector<std::string>  & setme)
   {
      if (p.content.empty()) // blank line
         add_line (setme, p.leader, p.content);
      else {
         const int max_content_width (tm.get_wrap_column() - p.leader.size());
         std::string tmp (p.content);
         wrap_line_at_column (&tmp[0], tmp.size(), max_content_width);
         StringView myp (tmp);
         StringView line;
         while (myp.pop_token (line, '\n'))
            add_line (setme, p.leader, line);
      }
   }
}

std::string
TextMassager :: fill (const StringView& body, bool flowed) const
{
   std::string retval;

   // get a temp copy of the body -- we don't wrap the signature.
   std::string tmp_body;
   std::string sig;
   for (StringView::const_iterator it=body.begin(), e=body.end(); it!=e; ++it)
      if (*it != '\r')
         tmp_body.push_back (*it);
   std::string::size_type sig_pos = tmp_body.find ("\n-- \n");
   if (sig_pos != std::string::npos) {
      sig = tmp_body.substr (sig_pos);
      tmp_body.erase (sig_pos);
   }

   // fill the paragraphs
   typedef std::vector<std::string> strings_t;
   typedef strings_t::const_iterator strings_cit;
   strings_t lines;
   paragraphs_t paragraphs (get_paragraphs (*this, tmp_body, flowed));
   for (p_it it=paragraphs.begin(), end=paragraphs.end(); it!=end; ++it)
      fill_paragraph (*this, *it, lines);

   // make a single string of all filled lines
   for (strings_cit it=lines.begin(), end=lines.end(); it!=end; ++it) {
      retval += *it;
      retval += '\n';
   }
   if (!retval.empty())
      retval.erase (retval.size()-1);

   // if we had a sig, put it back in
   if (!sig.empty()) {
      retval += '\n';
      retval += sig;
   }

   return retval;
}

/***
****
***/

std::string
TextMassager :: mute_quotes (const StringView& text) const
{
   std::string retval;
   const char * mute_str = _("> [quoted text muted]");

   StringView mytext (text);
   StringView line;

   bool last_line_was_quote = false;
   while (mytext.pop_token (line, '\n'))
   {
      const bool is_quote (!line.empty() && is_quote_character (g_utf8_get_char(line.str)));

      if (!is_quote)
      {
         retval.insert (retval.end(), line.begin(), line.end());
         retval += '\n';
      }
      else if (!last_line_was_quote)
      {
         retval += mute_str;
         retval += '\n';
      }

      last_line_was_quote = is_quote;
   }

   if (!retval.empty())
      retval.erase (retval.size()-1); // trim last \n

   return retval;
}

char*
TextMassager :: rot13_inplace (char * text)
{
   static bool inited = false;
   static char translate [UCHAR_MAX];

   if (!inited)
   {
      inited = true;
      const char * plain ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
      const char * roted ("nopqrstuvwxyzabcdefghijklmNOPQRSTUVWXYZABCDEFGHIJKLM");
      for (size_t i=0; i<UCHAR_MAX; ++i)
         translate[i] = (char)i;
      for (size_t i=0, len=strlen(plain); i!=len; ++i)
         translate[(size_t)plain[i]] = roted[i];
   }

   for (; text && *text; ++text)
      *text = translate[(unsigned char)*text];

   return text;
}

std::string
pan :: subject_to_path (const char * subjectline, bool full_subj, const std::string &separator)
{
  gchar *str1, *str2;
  const char *sep;
  std::string val;
  //stupid hack to silence the compiler
  GRegexCompileFlags cf0((GRegexCompileFlags)0);
  GRegexMatchFlags mf0((GRegexMatchFlags)0);

  if (separator.length() != 1)
    sep = "-";
  else switch (separator[0]) {
    case ' ':
    case '-':
    case '_': sep = separator.c_str(); break;
    default : sep = "-"; break;
  }

  if(!full_subj)
  {
    // strip out newspost/Xnews-style multi-part strings
    GRegex *mp1 =g_regex_new("\\s*(?:[Ff]ile|[Pp]ost)\\s[0-9]+\\s*(?:of|_)\\s*[0-9]+[:\\s]?", cf0, mf0, NULL);
    str1 = g_regex_replace_literal(mp1, subjectline, -1, 0, " ", mf0, NULL);
    g_regex_unref(mp1);

    // and the rest.  the last check is for pans collapsed part count
    GRegex *mp2 =g_regex_new("\\s*([\[(]?'?[0-9]+'?\\s*(?:of|/)\\s*'?[0-9]+'?.)|\\(/[0-9]+\\)", cf0, mf0, NULL);
    str2 = g_regex_replace_literal(mp2, str1, -1, 0, "", mf0, NULL);
    g_free(str1);
    g_regex_unref(mp2);

  // try to strip out the filename (may fail if it contains spaces)
  GRegex *fn =g_regex_new("\"[^\"]+?\" yEnc.*"    "|"
                          "\\S++\\s++yEnc.*"      "|"
                          "\"[^\"]+?\\.\\w{2,}\"" "|"
                          "\\S+\\.\\w{2,4}", cf0, mf0, NULL);
  str1 = g_regex_replace_literal(fn, str2, -1, 0, "", mf0, NULL);
  g_free(str2);
  g_regex_unref(fn);

    // try to strip out any byte counts
    GRegex *cnt =g_regex_new("\\[?[0-9]+\\s*(?:[Bb](ytes)?|[Kk][Bb]?)\\]?", cf0, mf0, NULL);
    str2 = g_regex_replace_literal(cnt, str1, -1, 0, "", mf0, NULL);
    g_free(str1);
    g_regex_unref(cnt);
  }
  else
    str2 = g_strdup(subjectline);

  // remove any illegal / annoying characters
  GRegex *badc =g_regex_new("[\\\\/<>|*?'\"\\.\\s]+", cf0, mf0, NULL);
  str1 = g_regex_replace_literal(badc, str2, -1, 0, sep, mf0, NULL);
  g_free(str2);
  g_regex_unref(badc);

  // remove any extraneous whitespace, '_', & '-'
  GRegex *ext =g_regex_new("[\\s_-]{2,}", cf0, mf0, NULL);
  str2 = g_regex_replace_literal(ext, str1, -1, 0, sep, mf0, NULL);
  g_free(str1);
  g_regex_unref(ext);

  // remove leading & trailing junk
  ext =g_regex_new("(^[\\s_-]+)|([\\s_-]+$)", cf0, mf0, NULL);
  str1 = g_regex_replace_literal(ext, str2, -1, 0, "", mf0, NULL);
  g_free(str2);
  g_regex_unref(ext);

  val=str1;
  g_free(str1);
  //std::cout << "\nSubject was: '" << subjectline << "'\nSubject now: '" << val << "'" << std::endl;
  return val;
}

std::string
pan :: expand_download_dir (const char * dir, const StringView& group)
{
  std::string val (dir);
  std::string::size_type pos;

  while (((pos = val.find ("%g"))) != val.npos)
    val.replace (pos, 2, group.str, group.len);

  std::string tmp (group.str, group.len);
  std::replace (tmp.begin(), tmp.end(), '.', G_DIR_SEPARATOR);
  while (((pos = val.find ("%G"))) != val.npos)
    val.replace (pos, 2, tmp);

  return val;
}

std::string
pan :: expand_download_dir_subject (const char * dir, const char * subjectline, const std::string &sep)
{
  std::string val (dir);
  std::string sub (subject_to_path(subjectline, false, sep));
  std::string::size_type pos;

  while (((pos = val.find ("%s"))) != val.npos)
    val.replace (pos, 2, sub);

  sub = subject_to_path(subjectline, true, sep);
  while (((pos = val.find ("%S"))) != val.npos)
    val.replace (pos, 2, sub);

  return val;
}

/*
"%n - Poster display name\n"
"%e - Poster e-mail address\n"
"%d - Current Date\n"
*/
std::string
pan :: expand_attachment_headers(const Quark& path, const Article& article)
{
  if (path.empty()) return std::string("");
  std::string val(path.c_str());
  std::string::size_type pos;
  std::string author_str (article.author.empty() ? "" : article.author);
  std::pair<std::string,std::string> author (get_email_address(author_str));
  std::string author_name (author.first);
  std::string author_email (author.second);

  EvolutionDateMaker ed;
  char *date = ed.get_date_string (article.time_posted);
  std::string now (date);
  g_free (date);

  if (author_name.empty())  author_name  = _("no_name");
  if (author_email.empty()) author_email = _("no_mail");

  while (((pos = val.find ("%n"))) != val.npos)
    val.replace (pos, 2, author_name);

  while (((pos = val.find ("%e"))) != val.npos)
    val.replace (pos, 2, author_email);

  while (((pos = val.find ("%d"))) != val.npos)
    val.replace (pos, 2, now);

  return val;
}

/** Separates user id into name and email address */
std::pair<std::string,std::string> pan :: get_email_address(std::string& s)
{
  std::pair<std::string,std::string> ret;
  size_t in  (s.find("<"));
  size_t out (s.find(">"));
  if (in == s.npos && out == s.npos)
  {
    if (s.find("@") != s.npos)
    {
      size_t bracket_in (s.find("("));
      size_t bracket_out (s.find(")"));
      if (bracket_in != s.npos && bracket_out != s.npos)
      {
        ret.first = s.substr(0, bracket_in-1);
        ret.second = s.substr(bracket_in+1, bracket_out-bracket_in-1);
      }
    }
  }
  else if (in == s.npos || out == s.npos)
  {
    return ret;
  }
  else
  {
    ret.first = s.substr(0,in-1);
    ret.second = s.substr (in+1,out-in-1);
  }

  return ret;
}
