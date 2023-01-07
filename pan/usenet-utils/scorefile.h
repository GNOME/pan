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

#ifndef pan_usenet_utils_scorefile_h_
#define pan_usenet_utils_scorefile_h_

#include <pan/usenet-utils/filter-info.h>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace pan {

class FileLineReader;
class LineReader;
class StringView;
class TextMatch;

/**
 * Handles slrn-style Score files.
 *
 * @ingroup usenet_utils
 */
struct Scorefile
{
  public:
    /**
     * Middleman to convert filenames to LineReaders. Useful for
     * replacing real files with mock data in unit tests.
     *
     * @ingroup data
     * @see Scorefile
     */
    struct FilenameToReader
    {
        virtual ~FilenameToReader();

        virtual LineReader *operator()(StringView const &filename) const;
    };

    /**
     * The default constructor creates a Scorefile that reads files from disk.
     *
     * Different file readers can be passed into the constructor by unit tests
     * that want to auto-generate the entries that the scorefile will parse.
     *
     * @warning This owns the instance you pass to it and deletes it on
     * destruction
     */
    explicit Scorefile(FilenameToReader *ftr = new FilenameToReader());

    virtual ~Scorefile();

    /** Parse the score file
     *
     * @returns true if file could be parsed, false otherwise
     */
    bool parse_file(StringView const &filename);

    void clear();

  public:
    /**
     * Used by build_score_string() to make a new slrn-style scorefile entry.
     * @see build_score_string
     * @ingroup data
     */
    struct AddItem
    {
        bool on;
        bool negate;
        std::string key;
        std::string value;
    };

    /**
     * Utility to generate text for a new slrn-style scorefile entry.
     * @see AddItem
     */
    static std::string build_score_string(StringView const &section_wildmat,
                                          int score_value,
                                          bool score_assign_flag,
                                          int lifespan_days,
                                          bool all_items_must_be_true,
                                          AddItem const *items,
                                          size_t item_count);

  public:
    /**
     * A scorefile entry.  One or more of these may be found in a
     * Scorefile::Section.
     * @see Section
     * @ingroup data
     */
    struct Item
    {
        std::string filename;
        size_t begin_line;
        size_t end_line;
        std::string name; // optional
        FilterInfo test;
        int value;
        bool value_assign_flag;
        bool expired;

        Item() :
          begin_line(0),
          end_line(0),
          value(0),
          value_assign_flag(false),
          expired(false)
        {
        }

        std::string describe() const
        {
          return test.describe();
        }
    };

    typedef std::deque<Item> items_t;

    /**
     * Represents a slrn scorefile's section, where a group of rules are to be
     * applied in a set of groups specified at the beginning of the section.
     * @see Item
     * @ingroup data
     */
    struct Section
    {
        std::string name;
        bool negate;
        std::deque<TextMatch> groups;
        std::deque<Item> items;
    };

    typedef std::deque<Section> sections_t;

  public:
    sections_t const &get_sections() const
    {
      return _sections;
    }

    void get_matching_sections(StringView const &groupname,
                               std::vector<Section const *> &setme) const;

  protected:
    sections_t _sections;

  private:
    Section *get_section(StringView const &name);

  private:
    // owned by Scorefile
    std::unique_ptr<FilenameToReader> _filename_to_reader;

    class ParseContext;
    bool parse_file(ParseContext &context, StringView const &filename);
    bool parse_line(StringView line,
                    StringView const &filename,
                    int line_number,
                    ParseContext &context);
};
} // namespace pan

#endif
