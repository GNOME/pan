#include "scorefile.h"

#include <pan/general/line-reader.h>
#include <pan/general/test.h>

#include <cassert>
#include <iostream>
#include <map>
#include <string>

using namespace pan;

struct MyFilenameToReader : public Scorefile::FilenameToReader
{
    virtual ~MyFilenameToReader()
    {
    }

    typedef std::map<std::string, std::string> files_t;
    files_t files;

    LineReader *operator()(StringView const &filename) const override
    {
      files_t::const_iterator it(files.find(filename));
      assert(it != files.end() && "wrong filename!");
      return new ScriptedLineReader(it->second);
    }
};

int main()
{
  MyFilenameToReader *my_ftr = new MyFilenameToReader();

  std::string const filename = "/home/charles/News/Score";
  my_ftr->files[filename] =
    "\n"
    "[news.software.readers]\n" // line 2
    "   Score: =1000\n"
    "   % All pan articles are good\n"
    "   Subject: pan\n"
    "\n"
    "   score: =900\n" // line 7
    "   % and slrn's not bad either\n"
    "   Subject: slrn\n"
    "\n"
    "   Score: 9999\n" // line 11
    "   %% Test the Has-Body keyword: mark anything cached as Watched.\n"
    "   Has-Body: 1\n"
    "\n"
    "   Score:: -100\n" // line 15
    "   # Let''s try some other tests.  score down short articles.\n"
    "   ~Lines: 10\n"
    "   ~Bytes: 80\n"
    "\n"
    "   Score: 222\n" // line 20
    "   % Test the Age keyword: Raise the score of anything posted yesterday "
    "or today.\n"
    "   Age: 1\n"
    "\n"
    "   Score: -333\n" // line 24
    "   %% Test negated age: Lower the score of anything more than a week "
    "old.\n"
    "   ~Age: 7\n"
    "\n"
    "   Score: -9999\n" // line 28
    "   %% Test ands\n"
    "   Xref: advocacy\n"
    "   ~From= Linus\n"
    "\n"
    "[alt.religion.kibology, comp.lang.*]\n"
    "\n"
    "   Score:: -1000\n" // line 35
    "   ~Subject: \\c[a-z]\n"
    "   {:\n"
    "     Subject: ^Re:\n"
    "     ~Subject: ^Re:.*\\c[a-z]\n"
    "   }\n"
    "   {::\n"
    "     Subject: ^Fwd:\n"
    "     ~Subject= ^Fwd:.*\\c[a-z]\n"
    "   }\n"
    "\n"
    "[alt.filters]\n"
    "   Score: -10\n"
    "   foo: filter\n";

  Scorefile scorefile(my_ftr);
  scorefile.parse_file(filename);
  Scorefile::sections_t const &sections(scorefile.get_sections());
  check_eq(sections.size(), 3)

  Scorefile::Section const &section(sections[0]);
  check(! section.negate)
  check_eq(section.name, "news.software.readers")
  check_eq(section.items.size(), 7)

  Scorefile::Item item(section.items[0]);
  check_eq(item.filename, filename)
  check_eq(item.begin_line, 3)
  check_eq(item.end_line, 5)
  check_eq(item.value, 1000)
  check(item.value_assign_flag)
  check_eq(item.expired, false)
  check_eq(item.expired, false)
  check_eq(item.test._type, FilterInfo::TEXT)
  check_eq(item.test._header, "Subject")
  check_eq(item.test._text.get_state().type, TextMatch::REGEX)
  check_eq(item.test._text.get_state().case_sensitive, false)
  check_eq(item.test._text.get_state().negate, false)
  check_eq(item.test._text.get_state().text, "pan")

  item = section.items[1];
  check_eq(item.filename, filename)
  check_eq(item.begin_line, 7)
  check_eq(item.end_line, 9)
  check_eq(item.value, 900)
  check(item.value_assign_flag)
  check_eq(item.expired, false)
  check_eq(item.expired, false)
  check_eq(item.test._type, FilterInfo::TEXT)
  check_eq(item.test._header, "Subject")
  check_eq(item.test._text.get_state().type, TextMatch::REGEX)
  check_eq(item.test._text.get_state().case_sensitive, false)
  check_eq(item.test._text.get_state().negate, false)
  check_eq(item.test._text.get_state().text, "slrn")

  // "   Score: 9999\n" // line 11
  //    "   %% Test the Has-Body keyword: mark anything cached as Watched.\n"
  //    "   Has-Body: 1\n"
  item = section.items[2];
  check_eq(item.filename, filename)
  check_eq(item.begin_line, 11)
  check_eq(item.end_line, 13)
  check_eq(item.value, 9999)
  check_eq(item.value_assign_flag, false)
  check_eq(item.expired, false)
  check_eq(item.test._type, FilterInfo::IS_CACHED)

  // "   Score: -100\n" // line 15
  // "   %% Let''s try some other tests.  score down short articles.\n"
  // "   ~Lines: 10\n"
  // "   ~Bytes: 80\n"
  item = section.items[3];
  check_eq(item.filename, filename)
  check_eq(item.begin_line, 15)
  check_eq(item.end_line, 18)
  check_eq(item.value, -100)
  check_eq(item.value_assign_flag, false)
  check_eq(item.expired, false)
  check_eq(item.test._type, FilterInfo::AGGREGATE_OR)
  check_eq(item.test._aggregates.size(), 2)
  check_eq(item.test._aggregates[0]->_type, FilterInfo::LINE_COUNT_GE)
  check_eq(item.test._aggregates[0]->_ge, 11)
  check(item.test._aggregates[0]->_negate != false)
  check_eq(item.test._aggregates[1]->_type, FilterInfo::BYTE_COUNT_GE)
  check_eq(item.test._aggregates[1]->_ge, 81)
  check(item.test._aggregates[1]->_negate != false)

  // "   Score: 222\n" // line 20
  // "   %% Test the Age keyword: Raise the score of anything posted yesterday
  // or today.\n" "   Age: 1\n"
  item = section.items[4];
  check_eq(item.filename, filename);
  check_eq(item.begin_line, 20)
  check_eq(item.end_line, 22)
  check_eq(item.value, 222)
  check_eq(item.value_assign_flag, false)
  check_eq(item.expired, false)
  check(item.test._negate != false)
  check_eq(item.test._type, FilterInfo::DAYS_OLD_GE)
  check_eq(item.test._ge, 2)

  // "   Score: -333\n" // line 24
  // "   %% Test negated age: Lower the score of anything more than a week
  // old.\n" "   ~Age: 7\n";
  item = section.items[5];
  check_eq(item.filename, filename);
  check_eq(item.begin_line, 24)
  check_eq(item.end_line, 26)
  check_eq(item.value, -333)
  check_eq(item.value_assign_flag, false)
  check_eq(item.expired, false)
  check_eq(item.test._negate, false)
  check_eq(item.test._type, FilterInfo::DAYS_OLD_GE)
  check_eq(item.test._ge, 8)

  // "   Score: -9999\n" // line 27
  // "   %% Test ands\n"
  // "   Xref: advocacy\n"
  // "   ~From= Linus\n"
  // "\n"
  item = section.items[6];
  check_eq(item.filename, filename);
  check_eq(item.begin_line, 28)
  check_eq(item.end_line, 31)
  check_eq(item.value, -9999)
  check_eq(item.value_assign_flag, false)
  check_eq(item.expired, false)
  check_eq(item.test._type, FilterInfo::AGGREGATE_AND)
  check_eq(item.test._aggregates.size(), 2)
  check_eq(item.test._aggregates[0]->_type, FilterInfo::TEXT)
  check_eq(item.test._aggregates[0]->_header, "Xref")
  check_eq(item.test._aggregates[0]->_negate, false)
  check_eq(item.test._aggregates[0]->_text.get_state().type, TextMatch::REGEX)
  check_eq(item.test._aggregates[0]->_text.get_state().case_sensitive, false)
  check_eq(item.test._aggregates[0]->_text.get_state().negate, false)
  check_eq(item.test._aggregates[0]->_text.get_state().text, "advocacy")
  check_eq(item.test._aggregates[1]->_type, FilterInfo::TEXT)
  check_eq(item.test._aggregates[1]->_header, "From")
  check_eq(item.test._aggregates[1]->_negate, true)
  check_eq(item.test._aggregates[1]->_text.get_state().type, TextMatch::REGEX)
  check_eq(item.test._aggregates[1]->_text.get_state().case_sensitive, true)
  check_eq(item.test._aggregates[1]->_text.get_state().negate, false)
  check_eq(item.test._aggregates[1]->_text.get_state().text, "Linus")

  /*
      "[alt.religion.kibology, comp.lang.*]\n"
      "\n"
      "   Score:: -1000\n" // line 35
      "   ~Subject: \\c[a-z]\n"
      "   {:\n"
      "     Subject: ^Re:\n"
      "     ~Subject: ^Re:.*\\c[a-z]\n"
      "   }\n"
      "   {::\n"
      "     Subject: ^Fwd:\n"
      "     ~Subject= ^Fwd:.*\\c[a-z]\n"
      "   }\n"
      "\n"
  */

  Scorefile::Section const &s(sections[1]);
  check_eq(s.negate, false)
  check_eq(s.name, "alt.religion.kibology, comp.lang.*")
  check_eq(s.items.size(), 1)
  check_eq(s.groups.size(), 2)
  check_eq(s.groups[0].get_state().type, TextMatch::REGEX)
  check_eq(s.groups[0].get_state().case_sensitive, true)
  check_eq(s.groups[0].get_state().negate, false)
  check_eq(s.groups[0].get_state().text, "^alt\\.religion\\.kibology$")
  check_eq(s.groups[1].get_state().type, TextMatch::REGEX)
  check_eq(s.groups[1].get_state().case_sensitive, true)
  check_eq(s.groups[1].get_state().negate, false)
  check_eq(s.groups[1].get_state().text, "^comp\\.lang\\..*$")

  item = s.items[0];
  check_eq(item.filename, filename)
  check_eq(item.begin_line, 35)
  check_eq(item.end_line, 44)
  check_eq(item.value, -1000)
  check_eq(item.value_assign_flag, false)
  check_eq(item.expired, false)
  check_eq(item.test._type, FilterInfo::AGGREGATE_OR)
  check_eq(item.test._aggregates.size(), 3)
  check_eq(item.test._aggregates[0]->_type, FilterInfo::TEXT)
  check(item.test._aggregates[0]->_negate)
  check_eq(item.test._aggregates[0]->_text.get_state().type, TextMatch::REGEX)
  check_eq(item.test._aggregates[0]->_text.get_state().negate, false)
  check_eq(item.test._aggregates[0]->_text.get_state().text, "\\c[a-z]")
  //
  check_eq(item.test._aggregates[1]->_type, FilterInfo::AGGREGATE_AND)
  check_eq(item.test._aggregates[1]->_aggregates.size(), 2)
  check_eq(item.test._aggregates[1]->_aggregates[0]->_type, FilterInfo::TEXT)
  check_eq(item.test._aggregates[1]->_aggregates[0]->_negate, false)
  check_eq(item.test._aggregates[1]->_aggregates[0]->_text.get_state().type,
           TextMatch::REGEX)
  check_eq(item.test._aggregates[1]->_aggregates[0]->_text.get_state().negate,
           false)
  check_eq(
    item.test._aggregates[1]->_aggregates[0]->_text.get_state().case_sensitive,
    false)
  check_eq(item.test._aggregates[1]->_aggregates[0]->_text.get_state().text,
           "^Re:")
  check_eq(item.test._aggregates[1]->_aggregates[1]->_type, FilterInfo::TEXT)
  check(item.test._aggregates[1]->_aggregates[1]->_negate)
  check_eq(item.test._aggregates[1]->_aggregates[1]->_text.get_state().type,
           TextMatch::REGEX)
  check_eq(
    item.test._aggregates[1]->_aggregates[1]->_text.get_state().case_sensitive,
    false)
  check_eq(item.test._aggregates[1]->_aggregates[1]->_text.get_state().text,
           "^Re:.*\\c[a-z]")
  //
  check_eq(item.test._aggregates[2]->_type, FilterInfo::AGGREGATE_OR)
  check_eq(item.test._aggregates[2]->_aggregates.size(), 2)
  check_eq(item.test._aggregates[2]->_aggregates[0]->_type, FilterInfo::TEXT)
  check_eq(item.test._aggregates[2]->_aggregates[0]->_negate, false)
  check_eq(item.test._aggregates[2]->_aggregates[0]->_text.get_state().type,
           TextMatch::REGEX)
  check_eq(item.test._aggregates[2]->_aggregates[0]->_text.get_state().negate,
           false)
  check_eq(
    item.test._aggregates[2]->_aggregates[0]->_text.get_state().case_sensitive,
    false)
  check_eq(item.test._aggregates[2]->_aggregates[0]->_text.get_state().text,
           "^Fwd:")
  check_eq(item.test._aggregates[2]->_aggregates[1]->_type, FilterInfo::TEXT)
  check(item.test._aggregates[2]->_aggregates[1]->_negate)
  check_eq(item.test._aggregates[2]->_aggregates[1]->_text.get_state().type,
           TextMatch::REGEX)
  check_eq(
    item.test._aggregates[2]->_aggregates[1]->_text.get_state().case_sensitive,
    true)
  check_eq(item.test._aggregates[2]->_aggregates[1]->_text.get_state().text,
           "^Fwd:.*\\c[a-z]")

  //     "[alt.filters]\n"
  //     "   Score: -10\n"
  //     "   foo: filter\n";
  Scorefile::Section const &s3(sections[2]);
  check_eq(s3.negate, false)
  check_eq(s3.name, "alt.filters")
  check_eq(s3.items.size(), 1)
  item = s3.items[0];
  check_eq(item.test._needs_body, true)

  return 0;
}
