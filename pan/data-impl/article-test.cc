#include "pan/data-impl/data-impl.h"
#include "pan/general/string-view.h"
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

char const *db_file("/tmp/article-test.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

class DataImplTest : public CppUnit::TestFixture
{
private:
    DataImpl *data;

public:
    void setUp()
    {
      StringView cache;
      Prefs prefs;
      // cleanup whatever may be left from previous test runs
      load_db_schema(pan_db);
      // always start from an empty db
      pan_db.exec(R"SQL(
         delete from article_part;
         delete from article_xref;
         delete from article_group;
         delete from article;
         delete from server_group;
         delete from `group`;
         delete from subject;
         delete from author;
      )SQL");
      data = new DataImpl(cache, prefs, true);
      pan_db.exec(R"SQL(
        insert into author (author)
               values ("Me <me@home>"),("Other <other@home>");
        insert into subject (subject) values ("blah");
      )SQL");
    }

    void tearDown()
    {
    }

    void add_article(std::string msg_id)
    {
      SQLite::Statement setup(pan_db, R"SQL(
        insert into article (message_id,author_id, subject_id, time_posted)
          values (?, (select id from author where author like "Me%"),
                     (select id from subject where subject = "blah"), 1234)
      )SQL");
      setup.bind(1, msg_id);
      int res (setup.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article " + msg_id, 1, res);
    }

    void add_article_part(std::string msg_id, std::string part_msg_id, int part_number, int size) {
      SQLite::Statement setup(pan_db, R"SQL(
        insert into article_part (article_id, part_message_id, part_number, size)
          values ((select id from article where message_id = ?), ?, ?, ?)
      )SQL");
      setup.bind(1, msg_id);
      setup.bind(2, part_msg_id);
      setup.bind(3, part_number);
      setup.bind(4, size);

      int res (setup.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article part " + msg_id + " part " + part_msg_id, 1, res);
    }

    void check_bytes(std::string mid, int expect) {
      SQLite::Statement check_bytes(pan_db, R"SQL(
        select bytes from article where message_id = ?
      )SQL");
      check_bytes.bind(1, mid);
      int result;
      while (check_bytes.executeStep()) {
          result = check_bytes.getColumn(0);
      }
      CPPUNIT_ASSERT_EQUAL_MESSAGE("article " + mid + " size ", expect, result);
    }

    void test_article_bytes()
    {
      std::string mid("a1");
      add_article(mid);
      add_article_part(mid, "a1.1", 1, 22);
      add_article_part(mid, "a1.2", 2, 33);

      std::string mid2("a2");
      add_article(mid2);
      add_article_part(mid2, "a2.1", 1, 2);
      add_article_part(mid2, "a2.2", 2, 3);

      check_bytes(mid, 55);
      check_bytes(mid2, 5);
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_article_bytes);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
