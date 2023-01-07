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

#include "scorefile.h"
#include "filter-info.h"

#include <pan/general/line-reader.h>
#include <pan/general/log.h>

#include <glib/gi18n.h>

#include <algorithm> // std::replace
#include <cstdlib>   // atoi, strtoul
#include <memory>
#include <sstream>
#include <string>

namespace pan {

/**
***  Age
**/
namespace {

unsigned long get_today()
{
  const time_t now(time(nullptr));
  struct tm t(*localtime(&now));
  return (t.tm_year * 10000) + (t.tm_mon * 100) + t.tm_mday;
}

} // namespace

/****
*****
*****  Parsing the scorefile
*****
****/

/**
 * private Scorefile class used when reading scorefiles from disk.
 */
class Scorefile::ParseContext
{
  public:
    ParseContext() :
      current_section(nullptr),
      today(get_today())
    {
    }

    Scorefile::Item *get_current_item()
    {
      Scorefile::Item *ret(nullptr);
      if (current_section != nullptr && ! current_section->items.empty())
      {
        ret = &current_section->items.back();
      }
      return ret;
    }

    FilterInfo *get_current_test()
    {
      FilterInfo *test(nullptr);
      Scorefile::Item *item(get_current_item());
      if (item)
      {
        test = &item->test;
        if (test)
        {
          for (auto offset : test_offsets)
          {
            test = test->_aggregates[offset];
          }
        }
      }
      return test;
    }

    void update_item_end_line(size_t line_number)
    {
      Scorefile::Item *item(get_current_item());
      if (item)
      {
        item->end_line = line_number;
      }
    }

    /**
     * 0 if it has not expired
     * 1 if it has expired
     * -1 if an error occurred while parsing
     */
    int has_score_expired(StringView const &v)
    {
      if (v.empty())
      {
        return 0;
      }

      const std::string tmp(v.str, v.len); // ensure zero termination for sscanf

      unsigned long mm, dd, yyyy;
      if (((3 != sscanf(tmp.c_str(), "%lu/%lu/%lu", &mm, &dd, &yyyy))
           && (3 != sscanf(tmp.c_str(), "%lu-%lu-%lu", &dd, &mm, &yyyy)))
          || (dd > 31) || (mm > 12) || (yyyy < 1900))
      {
        return -1;
      }

      unsigned long score_time = (yyyy - 1900) * 10000 + (mm - 1) * 100 + dd;
      return score_time <= today ? 1 : 0;
    }

    /** The current Section object, or NULL if none. */
    Scorefile::Section *current_section;

    /** For get_current_test() */
    std::vector<int> test_offsets;

  private:
    unsigned long today;
};

namespace {

std::string slrn_fix_regexp(StringView const &in)
{
  std::string s;
  s.reserve(in.len + 10); // 10 is a guess on how many extra chars we need
  s += '^';
  for (auto const &pch : in)
  {
    if (pch == '.' || pch == '+')
    {
      s += '\\';
    }
    else if (pch == '*')
    {
      s += '.';
    }
    s += pch;
  }
  if (s[s.size() - 1] != '$')
  {
    s += '$';
  }
  return s;
}

} // namespace

Scorefile::FilenameToReader::~FilenameToReader()
{
}

LineReader *Scorefile::FilenameToReader::operator()(
  StringView const &filename) const
{
  return new FileLineReader(filename);
}

Scorefile::Scorefile(FilenameToReader *ftr) :
  _filename_to_reader(ftr)
{
}

Scorefile::~Scorefile()
{
}

Scorefile::Section *Scorefile::get_section(StringView const &name)
{
  if (name.empty())
  {
    return nullptr;
  }

  // look for a section that already matches the name
  for (auto &section : _sections)
  {
    if (name == section.name)
    {
      return &section;
    }
  }

  // make a new section
  _sections.resize(_sections.size() + 1);
  Section &s(_sections.back());
  s.name = name;
  s.negate = *name.str == '~';

  // break the name into group tokens
  typedef std::vector<StringView> tokens_t;
  std::string tmp(name.str, name.len);
  std::replace(tmp.begin(), tmp.end(), ',', ' ');
  tokens_t tokens;
  StringView n(tmp);
  if (s.negate)
  {
    // walk past the negate tilde
    ++n.str;
    --n.len;
  }
  for (char const *pch(n.begin()), *e(n.end()); pch != e; ++pch)
  {
    while (pch != e && ::isspace(*pch))
    {
      ++pch;
    }
    if (pch == e)
    {
      break;
    }
    char const *tok_begin = pch;
    while (pch != e && ! ::isspace(*pch))
    {
      ++pch;
    }
    if (pch != tok_begin)
    {
      tokens.push_back(StringView(tok_begin, pch - tok_begin));
    }
    if (pch == e)
    {
      break;
    }
  }

  for (auto const &token : tokens)
  {
    const std::string groupname(slrn_fix_regexp(token));
    if (! TextMatch::validate_regex(groupname.c_str()))
    {
      continue;
    }
    TextMatch tm;
    tm.set(groupname, TextMatch::REGEX, true /*case*/, false /*negate*/);
    s.groups.push_back(tm);
  }

  return &s;
}

namespace {

/** Check line starts with prefix.
 *
 * Prefix will be discarded from line if so
 *
 * @param StringView line
 * @param char const *prefix
 *
 * @returns true if line starts with prefix, false otherwise
 */
bool check_and_strip(StringView &line, char const *prefix)
{
  std::size_t len = strlen(prefix);
  if (line.strncasecmp(prefix, len) != 0)
  {
    return false;
  }
  line.eat_chars(len);
  return true;
}

/** Check if a string matches, ignoring case */
bool eq_nocase(StringView const &val, char const *match)
{
  std::size_t len = strlen(match);
  return val.len == len && val.strncasecmp(match, len) == 0;
}

} // namespace

bool Scorefile::parse_file(ParseContext &context, StringView const &filename)
{
  std::unique_ptr<LineReader> in((*_filename_to_reader)(filename));
  if (! in)
  {
    return false;
  }

  size_t line_number(0);
  StringView line;
  while (in->getline(line))
  {
    line_number += 1;
    if (! parse_line(line, filename, line_number, context))
    {
      // error
      Log::add_err_va(
        _("Error reading score in %*.*s, line %d: unexpected line."),
        filename.len,
        filename.len,
        filename.str,
        line_number);
      return false;
    }
  }
  return true;
}

bool Scorefile::parse_line(StringView line,
                           StringView const &filename,
                           int line_number,
                           ParseContext &context)
{
  line.trim();
  // skip comments & blank lines
  if (line.empty() || *line.str == '%' || *line.str == '#')
  {
    return true;
  }

  if (check_and_strip(line, "include "))
  {
    // Include another file. This can be done anywhere
    context.update_item_end_line(line_number);

    line.trim();
    return parse_file(context, line);
  }

  if (*line.str == '[')
  {
    // new section
    StringView name(line.substr(line.str + 1, line.strchr(']')));
    name.trim();

    context.current_section = get_section(name);
    context.test_offsets.clear();
    return true;
  }

  if (context.current_section != nullptr && check_and_strip(line, "Score:"))
  {
    // new Item
    bool const all_tests_must_pass{line.empty() || *line.str != ':'};
    if (! all_tests_must_pass)
    {
      line.eat_chars(1);
    }
    line.ltrim();
    bool const value_assign_flag{! line.empty() && *line.str == '='};
    if (value_assign_flag)
    {
      line.eat_chars(1); // skip past the '='
    }
    line.ltrim();
    int const value(line.empty() ? 0 : atoi(line.str));
    StringView name;
    char const *hash = line.strchr('#');
    if (hash)
    {
      name = line.substr(hash + 1, nullptr);
    }
    name = name.substr(nullptr, name.strchr('%')); // skip trailing comments
    name.trim();

    Item item;
    item.name.assign(name.str, name.len);
    item.filename = filename;
    item.begin_line = line_number;
    item.value_assign_flag = value_assign_flag;
    item.value = value;
    if (all_tests_must_pass)
    {
      item.test.set_type_aggregate_and();
    }
    else
    {
      item.test.set_type_aggregate_or();
    }

    context.current_section->items.push_back(item);

    return true;
  }

  if (context.get_current_item() == nullptr)
  {
    return false;
  }

  if (check_and_strip(line, "{:"))
  {
    // begin nested condition
    context.update_item_end_line(line_number);
    bool const all_tests_must_pass{line.empty() || *line.str != ':'};
    if (! all_tests_must_pass)
    {
      line.eat_chars(1); // skip past the '{'
    }
    FilterInfo *test = new FilterInfo;
    if (all_tests_must_pass)
    {
      test->set_type_aggregate_and();
    }
    else
    {
      test->set_type_aggregate_or();
    }

    FilterInfo *parent(context.get_current_test());
    context.test_offsets.push_back(parent->_aggregates.size());
    parent->_aggregates.push_back(test);
  }
  else if (line.len >= 1 && *line.str == '}')
  {
    // end nested conditions
    context.update_item_end_line(line_number);
    context.test_offsets.resize(context.test_offsets.size() - 1);
  }
  else if (check_and_strip(line, "Expires:"))
  {
    // expiry date
    context.update_item_end_line(line_number);

    // get the date
    line.ltrim();
    int const has_expired(context.has_score_expired(line));
    if (has_expired < 0)
    {
      Log::add_err_va(_("Error reading score in %*.*s, line %d: expected "
                        "'Expires: MM/DD/YYYY' or 'Expires: DD-MM-YYYY'."),
                      filename.len,
                      filename.len,
                      filename.str,
                      line_number);
      return false;
    }
    else if (has_expired)
    {
      Log::add_info_va(_("Expired old score from %*.*s, line %d"),
                       filename.len,
                       filename.len,
                       filename.str,
                       line_number);
      Item *item = context.get_current_item();
      if (item)
      {
        item->expired = true;
      }
    }
  }
  else if (char const *delimiter = line.strpbrk(":="); delimiter != nullptr)
  {
    // new filter
    context.update_item_end_line(line_number);

    // follow XNews' idiom for specifying case sensitivity:
    // '=' as the delimiter instead of ':'
    bool const case_sensitive(*delimiter == '=');
    line.ltrim();
    bool negate(*line.str == '~');
    if (negate)
    {
      line.eat_chars(1);
    }

    StringView key(line.substr(nullptr, delimiter));
    key.trim();
    StringView val(line.substr(delimiter + 1, nullptr));
    val.trim();

    FilterInfo::aggregatesp_t &aggregates(
      context.get_current_test()->_aggregates);
    aggregates.push_back(new FilterInfo);
    FilterInfo &test(*aggregates.back());

    if (eq_nocase(key, "Lines"))
    {
      // "Lines: 5"  matches articles with > 5 lines.
      // "~Lines: 5" matches articles with <= 5 lines.
      unsigned long const gt = strtoul(val.str, NULL, 10);
      unsigned long const ge = gt + 1;
      test.set_type_line_count_ge(ge);
    }
    else if (eq_nocase(key, "Bytes"))
    {
      // bytes works the same way as lines.
      unsigned long const gt = strtoul(val.str, NULL, 10);
      unsigned long const ge = gt + 1;
      test.set_type_byte_count_ge(ge);
    }
    else if (eq_nocase(key, "Age"))
    {
      // age works differently from Lines and Bytes:
      // "Age: 7" matches articles <= 7 days old.
      unsigned long const le = strtoul(val.str, NULL, 10);
      test.set_type_days_old_le(le);
      negate = ! negate; // double negative: le is a negate state
    }
    else if (eq_nocase(key, "Has-Body"))
    {
      test.set_type_cached();
      if (val == "0")
      {
        negate = ! negate;
      }
    }
    else
    {
      TextMatch::Description d;
      d.type = TextMatch::REGEX;
      d.case_sensitive = case_sensitive;
      d.text.assign(val.str, val.len);
      test.set_type_text(key, d);
    }
    test._negate = negate;
  }
  else
  {
    return false;
  }

  return true;
}

namespace {

void normalize_test(FilterInfo *test)
{
  if (test->_type != FilterInfo::AGGREGATE_AND
      && test->_type != FilterInfo::AGGREGATE_OR)
  {
    return;
  }

  if (test->_aggregates.size() == 1)
  {
    *test = *test->_aggregates[0];
    normalize_test(test);
  }
  else
  {
    for (auto &item : test->_aggregates)
    {
      normalize_test(item);
    }
  }
}

} // namespace

void Scorefile::clear()
{
  _sections.clear();
}

bool Scorefile::parse_file(StringView const &filename)
{
  ParseContext context;
  if (! parse_file(context, filename))
  {
    return false;
  }

  size_t item_count(0);
  for (auto &section : _sections)
  {
    for (auto &item : section.items)
    {
      normalize_test(&item.test);
    }
    item_count += section.items.size();
  }

  if (! _sections.empty())
  {
    Log::add_info_va(_("Read %lu scoring rules in %lu sections from \"%s\""),
                     item_count,
                     _sections.size(),
                     filename.to_string().c_str());
  }
  return true;
}

void Scorefile::get_matching_sections(StringView const &groupname,
                                      std::vector<Section const *> &setme) const
{
  for (auto const &section : _sections)
  {
    for (auto const &group : section.groups)
    {
      bool match = group.test(groupname);
      if (section.negate)
      {
        match = ! match;
      }
      if (match)
      {
        setme.push_back(&section);
        break;
      }
    }
  }
}

std::string Scorefile::build_score_string(StringView const &section_wildmat,
                                          int score_value,
                                          bool score_assign_flag,
                                          int lifespan_days,
                                          bool all_items_must_be_true,
                                          AddItem const *items,
                                          size_t item_count)
{
  const time_t now(time(nullptr));
  std::ostringstream out;
  out << "%BOS\n"
      << "%Score created by Pan on " << ctime(&now) << '['
      << (section_wildmat.empty() ? "*" : section_wildmat) << "]\n"
      << "Score" << (all_items_must_be_true ? ":" : "::") << ' '
      << (score_assign_flag ? "=" : "") << score_value << '\n';
  if (lifespan_days > 0)
  {
    time_t expire_time_t = now + lifespan_days * 24 * 3600;
    struct tm expire_tm(*localtime(&expire_time_t));
    int dd = expire_tm.tm_mday;
    int mm = expire_tm.tm_mon + 1;
    int yyyy = expire_tm.tm_year + 1900;
    out << "Expires: " << mm << '/' << dd << '/' << yyyy << '\n';
  }
  for (size_t i(0); i != item_count; ++i)
  {
    Scorefile::AddItem const &item(items[i]);
    if (! item.value.empty())
    {
      out << (item.on ? "" : "%") << (item.negate ? "~" : "") << item.key
          << ": " << item.value << '\n';
    }
  }
  out << "%EOS";
  return out.str();
}

} // namespace pan
