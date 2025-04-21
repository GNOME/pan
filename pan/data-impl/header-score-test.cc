#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/general/string-view.h"
#include "pan/general/line-reader.h"
#include "pan/usenet-utils/scorefile.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <config.h>
#include <pan/data-impl/data-impl.h>
#include <pan/data/article.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/time-elapsed.h>
#include <pan/general/group-prefs.h>
#include <pan/gui/prefs-file.h>
#include <pan/usenet-utils/filter-info.h>
#include <string>

#include <cppunit/TestAssert.h>
#include <cppunit/TestResult.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

using namespace pan;

char const *db_file("/tmp/header-score-test.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

// copied from scorefile-test.cc
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

class DataImplTest : public CppUnit::TestFixture
{
private:
    DataImpl *data;
    HeaderFilter hf;

    struct FilterResult {
        std::string msg_id;
        bool pass;
    };

public:
    void setUp()
    {
      StringView cache;
      Prefs prefs;
      // cleanup whatever may be left from previous test runs
      load_db_schema(pan_db);
      pan_db.exec(R"SQL(
         delete from article_part;
         delete from article_xref;
         delete from article_group;
         delete from article;
         delete from server_group;
         delete from `group`;
         delete from author;
         delete from subject;
         delete from `server`;
      )SQL");
      data = new DataImpl(cache, prefs, true);
      pan_db.exec(R"SQL(
        insert into author (author)
               values ("Me <me@home>"),("Other <other@home>");
        insert into subject (subject) values ("blah");
        insert into server (host, port, pan_id, newsrc_filename)
                    values ("dummy", 2, 1, "/dev/null");
        insert into `group` (name) values ("g1"),("g2");
      )SQL");
    }

    void tearDown()
    {
    }

    void add_article(std::string msg_id,
                     std::string group,
                     std::string subject,
                     std::string author = "Me <me@home>",
                     int time_posted = 1234)
    {
      SQLite::Statement q_subject(pan_db, R"SQL(
        insert into subject (subject) values (?)
      )SQL");
      q_subject.bind(1, subject);
      int res = q_subject.exec();
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert subject " + subject, 1, res);

      SQLite::Statement q_article(pan_db, R"SQL(
        insert into article (message_id, author_id, subject_id, time_posted)
        values (?, (select id from author where author = ?),
                   (select id from subject where subject = ?), ?);
      )SQL");
      q_article.bind(1, msg_id);
      q_article.bind(2, author);
      q_article.bind(3, subject);
      q_article.bind(4, time_posted);
      res = q_article.exec();
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article " + msg_id, 1, res);

      add_article_in_group(msg_id, group);
    }

    void add_article_in_group(std::string msg_id, std::string group)
    {
      SQLite::Statement q_article_group(pan_db, R"SQL(
      insert into article_group(article_id, group_id)
        values ((select id from article where message_id = ?),
                (select id from `group` where name = ?));
      )SQL");
      q_article_group.bind(1, msg_id);
      q_article_group.bind(2, group);
      int res(q_article_group.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("link article " + msg_id + " in group" + group, 1, res);

      SQLite::Statement q_article_xref(pan_db, R"SQL(
      insert into article_xref(article_group_id, server_id, number)
        values ((select id from article_group where
                   article_id = (select id from article where message_id = ?)
                   and group_id = (select id from `group` where name = ?)
                ),
                (select id from `server` where host = "dummy"),
                1
               );
      )SQL");
      q_article_xref.bind(1, msg_id);
      q_article_xref.bind(2, group);
      q_article_xref.exec();
    }


    void test_score_article()
    {
      add_article("g1m1", "g1", "pan");
      add_article("g1m2", "g1", "other pan test");
      add_article("g1m3", "g1", "other test");

      MyFilenameToReader *my_ftr = new MyFilenameToReader();

      std::string const filename = "/home/notcharles/News/Score";
      my_ftr->files[filename] = "\n"
                                "[g1]\n" // line 2
                                "   Score: =1000\n"
                                "   % All pan articles are good\n"
                                "   Subject: pan\n"
                                "\n";
      Scorefile scorefile(my_ftr);
      scorefile.parse_file(filename);
      std::deque<Scorefile::Section> const &sections(scorefile.get_sections());

      hf.score_article(*data, sections, Article(Quark("g1"), Quark("g1m1")));
      hf.score_article(*data, sections, Article(Quark("g1"), Quark("g1m2")));
      hf.score_article(*data, sections, Article(Quark("g1"), Quark("g1m3")));

      SQLite::Statement q_score(
        pan_db,
        "select message_id, score from article_group "
        "join article on article_group.article_id = article.id");

      std::map<std::string, int> expect{{"g1m1", 1000}, {"g1m2", 1000},{"g1m3",0}};
      while(q_score.executeStep()) {
          std::string mid(q_score.getColumn(0));
          int score(q_score.getColumn(1));
          CPPUNIT_ASSERT_EQUAL_MESSAGE(mid + " score", expect[mid], score);
      }
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_score_article);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
