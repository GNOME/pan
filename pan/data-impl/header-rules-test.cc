#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-rules.h"
#include "pan/data/article-cache.h"
#include "pan/data/data.h"
#include "pan/general/string-view.h"
#include "pan/usenet-utils/rules-info.h"
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
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/ui/text/TestRunner.h>

using namespace pan;

char const *db_file("/tmp/data-impl-header-rules.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

class DataImplTest : public CppUnit::TestFixture
{
  private:
    DataImpl *data;
    HeaderRules hr;
    RulesInfo rules;

    struct FilterResult
    {
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
         delete from server;
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
      rules.clear();
      // That's dumb but that how it's done in header-pane.cc.
      rules.set_type_aggregate();
    }

    void tearDown()
    {
    }

    void _add_article(std::string msg_id)
    {
      SQLite::Statement q_article(pan_db, R"SQL(
        insert into article (message_id,author_id, time_posted)
          values (?, (select id from author where author like "Me%"), 1234);
      )SQL");
      q_article.bind(1, msg_id);
      int res(q_article.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article " + msg_id, 1, res);
    }

    void add_article(std::string group, std::string msg_id, int score = 0)
    {
      _add_article(msg_id);
      add_article_in_group(group, msg_id, score);
    }

    void add_article_in_group(std::string group, std::string msg_id, int score)
    {
      SQLite::Statement q_article_group(pan_db, R"SQL(
      insert into article_group(article_id, group_id, score)
        values ((select id from article where message_id = ?),
                (select id from `group` where name = ?),
                ?);
      )SQL");
      q_article_group.bind(1, msg_id);
      q_article_group.bind(2, group);
      q_article_group.bind(3, score);
      int res(q_article_group.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "link article " + msg_id + " in group" + group, 1, res);

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

    void assert_apply_result(std::string label, std::string group, int expect)
    {
      int nb = hr.apply_rules(*data, rules, group);

      auto str = "article " + label;
      CPPUNIT_ASSERT_EQUAL_MESSAGE(str + " count", expect, nb);
    }

    void test_empty_criteria()
    {
      add_article("g1", "m1");
      add_article("g1", "m2");

      assert_apply_result("empty", "g1", 0);
    }

    void test_mark_read()
    {
      add_article("g1", "m1", 150);
      add_article("g1", "m2");

      // set new rules with:
      RulesInfo *tmp = new RulesInfo;
      tmp->set_type_mark_read_b(100, 200);
      rules._aggregates.push_back(tmp);

      assert_apply_result("marked read article", "g1", 1);

      SQLite::Statement q(pan_db, R"SQL(
        select message_id from article where is_read == True;
      )SQL");
      while (q.executeStep())
      {
        std::string msg_id = q.getColumn(0);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
          "article marked as read", std::string("m1"), msg_id);
      }
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_empty_criteria);
    CPPUNIT_TEST(test_mark_read);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(DataImplTest::suite());
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
