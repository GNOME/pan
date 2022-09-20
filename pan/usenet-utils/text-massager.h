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

#ifndef __Text_Massager_h__
#define __Text_Massager_h__

#include <string>
#include <climits>
#include <set>
#include <pan/general/string-view.h>
#include <pan/general/quark.h>
#include <pan/data/article.h>

namespace pan
{
  /**
   * Used for manipluating text in ways that a newsreader might need:
   * <ul>
   * <li>rot13'ing text
   * <li>muting quoted text
   * <li>'filling' text to wrap at 78 cols
   * </ul>
   *
   * @ingroup usenet_utils
   */
  class TextMassager {
    public:
      TextMassager ();
      ~TextMassager ();

      TextMassager(TextMassager const &) = delete;
      TextMassager &operator=(TextMassager const &) = delete;

    public:
      static char* rot13_inplace (char * text);
      std::string mute_quotes (const StringView& text) const;
      std::string fill (const StringView& text, bool flowed = false) const;
      int get_wrap_column () const { return _wrap_column; }
      bool is_quote_character (unsigned int unichar) const;
      std::set<char> get_quote_characters () const;
      void set_wrap_column (int column) { _wrap_column = column; }
      void set_quote_characters (const std::set<char>& quote_chars);
    private:
      int _wrap_column;
      char * _quote_characters;
  };

 /**
   * Used to convert a subject line to a path for saving articles.
   *
   * @ingroup usenet_utils
   */
   std::string subject_to_path (const char * subjectline, bool full_subj, const std::string &separator);

   std::string expand_download_dir_subject (const char * dir, const char * subjectline, const std::string &sep);
   std::string expand_download_dir (const char * dir, const StringView& group);
   std::string expand_attachment_headers(const Quark& path, const Article& article);

   std::pair<std::string,std::string> get_email_address(std::string& s);

}

#endif
