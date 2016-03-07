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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <algorithm> // std::replace
#include <cstdlib> // atoi, strtoul
#include <iostream>
#include <sstream>
#include <string>
extern "C" {
  #include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include "filter-info.h"
#include "scorefile.h"

using namespace pan;

/**
***  Age
**/
namespace
{
  unsigned long get_today ()
  {
    const time_t now (time (0));
    struct tm t (*localtime (&now));
    return (t.tm_year*10000) + (t.tm_mon*100) + t.tm_mday;
  }

  /**
   * 0 if it has not expired
   * 1 if it has expired
   * -1 if an error occurred while parsing
   */
  int
  has_score_expired (const StringView& v, unsigned long today)
  {
    if (v.empty())
      return 0;

    const std::string tmp (v.str, v.len); // ensure zero termination for sscanf

    unsigned long mm, dd, yyyy;
    if (((3 != sscanf (tmp.c_str(), "%lu/%lu/%lu", &mm, &dd, &yyyy))
      && (3 != sscanf (tmp.c_str(), "%lu-%lu-%lu", &dd, &mm, &yyyy)))
      || (dd > 31)
      || (mm > 12)
      || (yyyy < 1900))
      return -1;

    unsigned long score_time = (yyyy - 1900) * 10000 + (mm - 1) * 100 + dd;
    return score_time <= today ? 1 : 0;
  }
}

/****
*****
*****  Parsing the scorefile
*****
****/

/**
 * private Scorefile class used when reading scorefiles from disk.
 */
struct pan::Scorefile::ParseContext
{
  /** The current Section object, or NULL if none. */
  Scorefile::Section * current_section;

  /** For get_current_test() */
  std::vector<int> test_offsets;

  Scorefile::Item * get_current_item () {
    Scorefile::Item * ret (0);
    if (current_section!=0 && !current_section->items.empty())
      ret = &current_section->items.back();
    return ret;
  }

  FilterInfo * get_current_test () {
    FilterInfo * test (0);
    Scorefile::Item * item (get_current_item());
    if (item)
      test = &item->test;
    if (test)
      foreach_const (std::vector<int>, test_offsets, it)
        test = test->_aggregates[*it];
    return test;
  }

  void update_item_end_line (size_t line_number) {
    Scorefile::Item * item (get_current_item());
    if (item)
      item->end_line = line_number;
  }

  unsigned long today;

  ParseContext (): current_section(0), today(get_today()) {}
};


namespace
{
  std::string slrn_fix_regexp (const StringView& in)
  {
    std::string s;
    s.reserve (in.len + 10); // 10 is a guess on how many extra chars we need
    s += '^';
    for (const char *pch(in.begin()), *end(in.end()); pch!=end; ++pch) {
      if (*pch=='.' || *pch=='+')
        s += '\\';
      else if (*pch=='*')
        s += '.';
      s += *pch;
    }
    if (s[s.size()-1]!='$')
      s += '$';
    return s;
  }
}

Scorefile :: Section*
Scorefile :: get_section (const StringView& name)
{
  if (name.empty())
    return 0;

  // look for a section that already matches the name
  foreach (sections_t, _sections, it)
    if (name == it->name)
      return &*it;

  // make a new section
  _sections.resize (_sections.size()+1);
  Section& s (_sections.back());
  s.name = name;
  s.negate = *name.str=='~';

  // break the name into group tokens
  typedef std::vector<StringView> tokens_t;
  std::string tmp (name.str, name.len);
  std::replace (tmp.begin(), tmp.end(), ',', ' ');
  tokens_t tokens;
  StringView n (tmp);
  if (s.negate) { ++n.str; --n.len; } // walk past the negate tilde
  for (const char *pch(n.begin()), *e(n.end()); pch!=e; ++pch) {
    while (pch!=e && ::isspace(*pch)) ++pch;
    if (pch==e) break;
    const char * tok_begin = pch;
    while (pch!=e && !::isspace(*pch)) ++pch;
    if (pch!=tok_begin)
      tokens.push_back (StringView (tok_begin, pch-tok_begin));
    if (pch==e)
      break;
  }

  foreach_const (tokens_t, tokens, it) {
    const std::string groupname (slrn_fix_regexp (*it));
    if (!TextMatch::validate_regex (groupname.c_str()))
      continue;
    TextMatch tm;
    tm.set  (groupname, TextMatch::REGEX, true/*case*/, false/*negate*/);
    s.groups.push_back (tm);
  }

  return &s;
}

int
Scorefile :: parse_file (ParseContext& context, const StringView& filename)
{
  int retval (0);

  LineReader * in ((*_filename_to_reader)(filename));
  if (!in)
    return -1;

  size_t line_number (0);
  StringView line;
  while (in->getline (line))
  {
    ++line_number;
    //std::cerr << LINE_ID << " line " << line_number << " [" << line << ']' << std::endl;

    line.trim ();

    // skip comments & blank lines
    if (line.empty() || *line.str=='%' || *line.str=='#')
      continue;

    // new section
    if (*line.str=='[')
    {
      StringView name (line.substr (line.str+1, line.strchr(']')));
      name.trim ();

      context.current_section = get_section (name);
      context.test_offsets.clear ();
    }

    // new Item
    else if (context.current_section!=0 && !line.strncasecmp("Score:",6))
    {
      line.eat_chars (6);
      const bool all_tests_must_pass (line.len>=2 && !memcmp(line.str,"::",2));
      while (!line.empty() && *line.str==':') line.eat_chars (1);
      while (!line.empty() && ::isspace(*line.str)) line.eat_chars (1);
      const bool value_assign_flag = (!line.empty() && *line.str=='=');
      if (value_assign_flag) line.eat_chars(1); // skip past the '='
      while (!line.empty() && ::isspace(*line.str)) line.eat_chars (1);
      const int value (line.empty() ? 0 : atoi(line.str));
      StringView name;
      const char * hash = line.strchr ('#');
      if (hash)
        name = line.substr (hash+1, 0);
      name = name.substr (0, name.strchr('%')); // skip trailing comments
      name.trim ();

      std::deque<Item>& items (context.current_section->items);
      items.resize (items.size() + 1);
      Item& item (items.back());
       
      item.name.assign (name.str, name.len);
      item.filename = filename;
      item.begin_line = line_number;
      item.value_assign_flag = value_assign_flag;
      item.value = value;
      if (all_tests_must_pass)
        item.test.set_type_aggregate_and ();
      else
        item.test.set_type_aggregate_or ();
    }

    // begin nested condition
    else if (line.len>=2 && line.str[0]=='{' && line.str[1]==':' && context.get_current_test()!=0)
    {
      context.update_item_end_line (line_number);

      line.eat_chars (1); // skip past the '{'
      const bool only_one_test_must_pass (line.len>=2 && !memcmp(line.str,"::",2));
      FilterInfo *test = new FilterInfo;
      if (only_one_test_must_pass)
        test->set_type_aggregate_or ();
      else
        test->set_type_aggregate_and ();

      FilterInfo * parent (context.get_current_test ());
      context.test_offsets.push_back (parent->_aggregates.size());
      parent->_aggregates.push_back (test);
    }

    // end nested conditions
    else if (line.len>=1 && *line.str=='}' && context.get_current_test()!=0)
    {
      context.update_item_end_line (line_number);
      context.test_offsets.resize (context.test_offsets.size()-1);
    }

    // include another file
    else if (!line.strncasecmp ("include ", 8))
    {
      context.update_item_end_line (line_number);

      StringView new_filename (line);
      new_filename.eat_chars (8);
      new_filename.trim();
      const int status (parse_file (context, new_filename));
      if (status != 0) {
        retval = status;
        break;
      }
    }

    // include another file
    else if (!line.strncasecmp("Expires:", 6) && context.get_current_test()!=0)
    {
      context.update_item_end_line (line_number);

      // get the date
      line.eat_chars (8);
      line.trim ();
      const int has_expired (has_score_expired (line, context.today));
      if (has_expired < 0)
        Log::add_err_va (_("Error reading score in %*.*s, line %d: expected 'Expires: MM/DD/YYYY' or 'Expires: DD-MM-YYYY'."),
          filename.len, filename.len, filename.str, line_number);
      else if (has_expired) {
        Log::add_info_va (_("Expired old score from %*.*s, line %d"),
          filename.len, filename.len, filename.str, line_number);
        Item * item = context.get_current_item ();
        if (item)
          item->expired = true;
      }
    }

    // new filter
    else if (line.strpbrk (":=") && context.get_current_item()!=0)
    {
      context.update_item_end_line (line_number);
      
      // follow XNews' idiom for specifying case sensitivity:
      // '=' as the delimiter instead of ':' 
      const char * delimiter = line.strpbrk (":=");
      const bool case_sensitive (*delimiter=='=');

      line.trim ();
      bool negate (*line.str=='~');
      if (negate) line.eat_chars (1);

      StringView key (line.substr (0, delimiter));
      key.trim ();
      StringView val (line.substr (delimiter+1, 0));
      val.trim ();

      FilterInfo::aggregatesp_t& aggregates (context.get_current_test()->_aggregates);
      aggregates.push_back (new FilterInfo);
      FilterInfo& test (*aggregates.back());
 
      if (!key.strncasecmp ("Lines", 5))
      {
        // "Lines: 5"  matches articles with > 5 lines.
        // "~Lines: 5" matches articles with <= 5 lines.
        const unsigned long gt = strtoul (val.str, NULL, 10);
        const unsigned long ge = gt + 1;
        test.set_type_line_count_ge (ge);
      }
      else if (!key.strncasecmp("Bytes", 5))
      {
        // bytes works the same way as lines.
        const unsigned long gt = strtoul (val.str, NULL, 10);
        const unsigned long ge = gt + 1;
        test.set_type_byte_count_ge (ge);
      }
      else if (!key.strncasecmp ("Age", 3))
      {
        // age works differently from Lines and Bytes:
        // "Age: 7" matches articles <= 7 days old.
        const unsigned long le = strtoul (val.str, NULL, 10);
        test.set_type_days_old_le (le);
        negate = !negate; // double negative: le is a negate state
      }
      else if (!key.strncasecmp ("Has-Body", 8))
      {
        test.set_type_cached ();
        if (val == "0")
          negate = !negate;
      }
      else
      {
        TextMatch::Description d;
        d.type = TextMatch::REGEX;
        d.case_sensitive = case_sensitive;
        d.text.assign (val.str, val.len);
        test.set_type_text (key, d);
      }
      test._negate = negate;
    }

    // error
    else {
      Log::add_err_va (_("Error reading score in %*.*s, line %d: unexpected line."),
        filename.len, filename.len, filename.str, line_number);
      retval = -1;
      break;
    }
  }

  delete in;
  return retval;
}

namespace
{
  void normalize_test (FilterInfo *test)
  {
    if ((test->_type!=test->AGGREGATE_AND) && (test->_type!=test->AGGREGATE_OR))
      return;

    if (test->_aggregates.size() == 1) {
      *test = *test->_aggregates[0];
      normalize_test (test);
    } else foreach (FilterInfo::aggregatesp_t, test->_aggregates, it)
      normalize_test (*it);
  }
}

void
Scorefile :: clear ()
{
  _sections.clear ();
}

int
Scorefile :: parse_file (const StringView& filename)
{
  ParseContext context;
  const int err (parse_file (context, filename));
  if (err)
    return err;

  foreach (sections_t, _sections, sit)
    foreach (items_t, sit->items, it)
      normalize_test (&it->test);

  size_t item_count (0);
  foreach (sections_t, _sections, sit)
    item_count += sit->items.size ();

  if (!_sections.empty())
    Log::add_info_va (_("Read %lu scoring rules in %lu sections from \"%s\""),
      item_count, _sections.size(), filename.to_string().c_str());
  return 0;
}

void
Scorefile :: get_matching_sections (const StringView& groupname, std::vector<const Section*>& setme) const
{
  foreach_const (sections_t, _sections, sit)
  {
    bool match (false);
    foreach_const (std::deque<TextMatch>, sit->groups, git) {
      match = git->test (groupname);
      if (sit->negate) match = !match;
      if (match) break;
    }
    if (match)
      setme.push_back (&*sit);
  }
}

std::string
Scorefile :: build_score_string (const StringView    & section_wildmat,
                                 int                   score_value,
                                 bool                  score_assign_flag,
                                 int                   lifespan_days,
                                 bool                  all_items_must_be_true,
                                 const AddItem       * items,
                                 size_t                item_count)
{
  const time_t now (time (0));
  std::ostringstream out;
  out << "%BOS" << std::endl
      << "%Score created by Pan on " << ctime(&now)
      << "[" << (section_wildmat.empty() ? "*" : section_wildmat) << ']' << std::endl
      << "Score" << (all_items_must_be_true ? ":" : "::") << " " << (score_assign_flag?"=":"") << score_value << std::endl;
  if (lifespan_days > 0) {
    time_t expire_time_t = now + lifespan_days * 24 * 3600;
    struct tm expire_tm (*localtime (&expire_time_t));
    int dd = expire_tm.tm_mday;
    int mm = expire_tm.tm_mon + 1;
    int yyyy = expire_tm.tm_year + 1900;
    out << "Expires: " << mm << '/' << dd << '/' << yyyy << std::endl;
  }
  for (size_t i(0); i!=item_count; ++i) {
    const Scorefile::AddItem& item (items[i]);
    if (!item.value.empty())
      out << (item.on ? "" : "%") << (item.negate ? "~" : "") << item.key << ": " << item.value << std::endl;
  }
  out << "%EOS";
  return out.str ();
}
