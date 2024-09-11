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

#ifndef _TextMatch_h_
#define _TextMatch_h_

#include <pan/general/string-view.h>
#include <string>

namespace pan {
/**
 * Encapsulates regular expression and efficient case-insensitive text matching.
 *
 * @ingroup general
 */
class TextMatch
{
  public:
    enum Type
    {
      CONTAINS,
      IS,
      BEGINS_WITH,
      ENDS_WITH,
      REGEX
    };

    /**
     * Specifies how a TextMatch object should compare its text.
     */
    struct Description
    {
        Type type;
        bool case_sensitive;
        bool negate;
        std::string text;

        void clear()
        {
          type = IS;
          case_sensitive = negate = false;
          text.clear();
        }

        Description()
        {
          clear();
        }
    };

  public:
    TextMatch();
    TextMatch(TextMatch const &that);
    TextMatch &operator=(TextMatch const &that);
    ~TextMatch();

    void clear();

    void set(StringView const &text,
             Type type,
             bool case_sensitive,
             bool negate = false);

    void set(Description const &d)
    {
      set(d.text, d.type, d.case_sensitive, d.negate);
    }

    bool test(StringView const &text) const;

    static std::string create_regex(StringView const &, Type);

    static bool validate_regex(char const *regex);

    Description const &get_state() const
    {
      return state;
    }

  private:
    /** This is the state passed into set()... */
    Description state;

  public:
    /** The real state we use.  This is the same as state._type
        unless state._type is REGEX and we can do it faster with
        another type (i.e. "^hello" can be done with strncmp()
        instead of a regex invocation */
    Type _impl_type;

    /** This real string we use.  See _impl_type for description. */
    std::string _impl_text;

    char *_skip;

  private:
    class PcreInfo;
    mutable PcreInfo *_pcre_info;

    enum PcreState
    {
      NEED_COMPILE,
      COMPILED,
      ERR
    };

    mutable PcreState _pcre_state;

  public:
    int my_regexec(StringView const &) const;
};
} // namespace pan

#endif
