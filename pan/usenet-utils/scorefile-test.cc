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

  my_ftr->files["/home/charles/News/Score"] =
    "\n"
    "[news.software.readers]\n" // line 2
    "   Score: =1000\n"
    "   % All pan articles are good\n"
    "   Subject: pan\n"
    "\n"
    "   Score: =900\n" // line 7
    "   % and slrn's not bad either\n"
    "   Subject: slrn\n"
    "\n"
    "   Score: 9999\n" // line 11
    "   %% Test the Has-Body keyword: mark anything cached as Watched.\n"
    "   Has-Body: 1\n"
    "\n"
    "   Score: -100\n" // line 15
    "   %% Let''s try some other tests.  score down short articles.\n"
    "   ~Lines: 10\n"
    "   ~Bytes: 80\n"
    "\n"
    "   Score: 222\n" // line 20
    "   %% Test the Age keyword: Raise the score of anything posted yesterday "
    "or today.\n"
    "   Age: 1\n"
    "\n"
    "   Score: -333\n" // line 24
    "   %% Test negated age: Lower the score of anything more than a week "
    "old.\n"
    "   ~Age: 7\n"
    "\n"
    "[alt.religion.kibology, comp.lang.*]\n"
    "\n"
    "   Score:: -1000\n" // line 30
    "   ~Subject: \\c[a-z]\n"
    "   {:\n"
    "     Subject: ^Re:\n"
    "     ~Subject: ^Re:.*\\c[a-z]\n"
    "   }\n"
    "\n"
    "[alt.filters]\n"
    "   Score: -10\n"
    "   foo: filter\n";

  const std::string filename = "/home/charles/News/Score";
  Scorefile scorefile(my_ftr);
  scorefile.parse_file(filename);
  Scorefile::sections_t const &sections(scorefile.get_sections());
  check(sections.size() == 3)

  Scorefile::Section const &section(sections[0]);
  check(! section.negate)
  check(section.name == "news.software.readers")
  check(section.items.size() == 6)

  Scorefile::Item item(section.items[0]);
  check(item.filename == filename)
  check(item.begin_line == 3)
  check(item.end_line == 5)
  check(item.value == 1000)
  check(item.value_assign_flag)
  check(item.expired == false)
  check(item.expired == false)
  check(item.test._type == FilterInfo::TEXT)
  check(item.test._header == "Subject")
  check(item.test._text.get_state().type == TextMatch::REGEX)
  check(item.test._text.get_state().case_sensitive == false)
  check(item.test._text.get_state().negate == false)
  check(item.test._text.get_state().text == "pan")

  item = section.items[1];
  check(item.filename == filename)
  check(item.begin_line == 7)
  check(item.end_line == 9)
  check(item.value == 900)
  check(item.value_assign_flag)
  check(item.expired == false)
  check(item.expired == false)
  check(item.test._type == FilterInfo::TEXT)
  check(item.test._header == "Subject")
  check(item.test._text.get_state().type == TextMatch::REGEX)
  check(item.test._text.get_state().case_sensitive == false)
  check(item.test._text.get_state().negate == false)
  check(item.test._text.get_state().text == "slrn")

  // "   Score: 9999\n" // line 11
  //    "   %% Test the Has-Body keyword: mark anything cached as Watched.\n"
  //    "   Has-Body: 1\n"
  item = section.items[2];
  check(item.filename == filename)
  check(item.begin_line == 11)
  check(item.end_line == 13)
  check(item.value == 9999)
  check(item.value_assign_flag == false)
  check(item.expired == false)
  check(item.test._type == FilterInfo::IS_CACHED)

  // "   Score: -100\n" // line 15
  // "   %% Let''s try some other tests.  score down short articles.\n"
  // "   ~Lines: 10\n"
  // "   ~Bytes: 80\n"
  item = section.items[3];
  check(item.filename == filename)
  check(item.begin_line == 15)
  check(item.end_line == 18)
  check(item.value == -100)
  check(item.value_assign_flag == false)
  check(item.expired == false)
  check(item.test._type == FilterInfo::AGGREGATE_OR)
  check(item.test._aggregates.size() == 2)
  check(item.test._aggregates[0]->_type == FilterInfo::LINE_COUNT_GE)
  check(item.test._aggregates[0]->_ge == 11)
  check(item.test._aggregates[0]->_negate != false)
  check(item.test._aggregates[1]->_type == FilterInfo::BYTE_COUNT_GE)
  check(item.test._aggregates[1]->_ge == 81)
  check(item.test._aggregates[1]->_negate != false)

  // "   Score: 222\n" // line 20
  // "   %% Test the Age keyword: Raise the score of anything posted yesterday
  // or today.\n" "   Age: 1\n"
  item = section.items[4];
  check(item.filename == filename);
  check(item.begin_line == 20)
  check(item.end_line == 22)
  check(item.value == 222)
  check(item.value_assign_flag == false)
  check(item.expired == false)
  check(item.test._negate != false)
  check(item.test._type == FilterInfo::DAYS_OLD_GE)
  check(item.test._ge == 2)

  // "   Score: -333\n" // line 24
  // "   %% Test negated age: Lower the score of anything more than a week
  // old.\n" "   ~Age: 7\n";
  item = section.items[5];
  check(item.filename == filename);
  check(item.begin_line == 24)
  check(item.end_line == 26)
  check(item.value == -333)
  check(item.value_assign_flag == false)
  check(item.expired == false)
  check(item.test._negate == false)
  check(item.test._type == FilterInfo::DAYS_OLD_GE)
  check(item.test._ge == 8)

  //"[alt.religion.kibology, comp.lang.*]\n"
  //"\n"
  //"   Score:: -1000\n" // line 30
  //"   ~Subject: \\c[a-z]\n"
  //"   {:\n"
  //"     Subject: ^Re:\n"
  //"     ~Subject: ^Re:.*\\c[a-z]\n"
  //"   }\n";

  Scorefile::Section const &s(sections[1]);
  check(s.negate == false)
  check(s.name == "alt.religion.kibology, comp.lang.*")
  check(s.items.size() == 1)
  check(s.groups.size() == 2)
  check(s.groups[0].get_state().type == TextMatch::REGEX)
  check(s.groups[0].get_state().case_sensitive == true)
  check(s.groups[0].get_state().negate == false)
  check(s.groups[0].get_state().text == "^alt\\.religion\\.kibology$")
  check(s.groups[1].get_state().type == TextMatch::REGEX)
  check(s.groups[1].get_state().case_sensitive == true)
  check(s.groups[1].get_state().negate == false)
  check(s.groups[1].get_state().text == "^comp\\.lang\\..*$")

  item = s.items[0];
  check(item.filename == filename)
  check(item.begin_line == 30)
  check(item.end_line == 35)
  check(item.value == -1000)
  check(item.value_assign_flag == false)
  check(item.expired == false)
  check(item.test._type == FilterInfo::AGGREGATE_OR)
  check(item.test._aggregates.size() == 2)
  check(item.test._aggregates[0]->_type == FilterInfo::TEXT)
  check(item.test._aggregates[0]->_negate)
  check(item.test._aggregates[0]->_text.get_state().type == TextMatch::REGEX)
  check(item.test._aggregates[0]->_text.get_state().negate == false)
  check(item.test._aggregates[0]->_text.get_state().text == "\\c[a-z]")
  check(item.test._aggregates[1]->_type == FilterInfo::AGGREGATE_AND)
  check(item.test._aggregates[1]->_aggregates.size() == 2)
  check(item.test._aggregates[1]->_aggregates[0]->_type == FilterInfo::TEXT)
  check(item.test._aggregates[1]->_aggregates[0]->_negate == false)
  check(item.test._aggregates[1]->_aggregates[0]->_text.get_state().type
        == TextMatch::REGEX)
  check(item.test._aggregates[1]->_aggregates[0]->_text.get_state().negate
        == false)
  check(item.test._aggregates[1]->_aggregates[0]->_text.get_state().case_sensitive
        == false)
  check(item.test._aggregates[1]->_aggregates[0]->_text.get_state().text
        == "^Re:")
  check(item.test._aggregates[1]->_aggregates[1]->_type == FilterInfo::TEXT)
  check(item.test._aggregates[1]->_aggregates[1]->_negate)
  check(item.test._aggregates[1]->_aggregates[1]->_text.get_state().type
        == TextMatch::REGEX)
  check(item.test._aggregates[1]->_aggregates[1]->_text.get_state().case_sensitive
        == false)
  check(item.test._aggregates[1]->_aggregates[1]->_text.get_state().text
        == "^Re:.*\\c[a-z]")

  //     "[alt.filters]\n"
  //     "   Score: -10\n"
  //     "   foo: filter\n";
  Scorefile::Section const &s3(sections[2]);
  check(s3.negate == false)
  check(s3.name == "alt.filters")
  check(s3.items.size() == 1)
  item = s3.items[0];
  check(item.test._needs_body == true)

  return 0;
}
