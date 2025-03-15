#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data/data.h"
#include "pan/general/string-view.h"
#include "pan/general/text-match.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <config.h>
#include <cstdio>
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

char const *db_file("/tmp/header-filter-test.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

class DataImplTest : public CppUnit::TestFixture
{
private:
    DataImpl *data;
    HeaderFilter hf;
    FilterInfo criteria;

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
         delete from server;
         delete from `group`;
         delete from profile;
         delete from author;
         delete from subject;
      )SQL");
      data = new DataImpl(cache, prefs, true);
      pan_db.exec(R"SQL(
        insert into author (author)
               values ("Me <me@home>"),("Other <other@home>");
        insert into subject (subject) values ("blah");
        insert into server (host, port, pan_id, newsrc_filename)
                    values ("dummy", 2, 1, "/dev/null");
        insert into `group` (name) values ("g1"),("g2");
        insert into current_group ( group_id )
          select id from `group` as g where g.name = "g1";
        insert into profile (name, author_id, server_id)
                    values (
                      "my_profile",
                      (select id from author where author like "Me%"),
                      (select id from server where host = "dummy")
                    );
      )SQL");
      criteria.clear();
      // That's dumb but that how it's done in header-pane.cc.
      criteria.set_type_aggregate_and();
    }

    void tearDown()
    {
    }

    void add_article(std::string msg_id)
    {
      SQLite::Statement q_article(pan_db, R"SQL(
        insert into article (message_id,subject_id, author_id, time_posted)
        values (?, (select id from subject where subject = "blah"),
                   (select id from author where author like "Me%"), 1234);
      )SQL");
      q_article.bind(1, msg_id);
      int res (q_article.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article " + msg_id, 1, res);
    }

    void add_article(std::string msg_id, std::string group)
    {
      add_article(msg_id);
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

    void assert_result(std::vector<std::string> expect)
    {
        assert_result("",expect);
    }

    void assert_result(std::string step, std::vector<std::string> expect)
    {
      auto q = hf.get_sql_query(*data, criteria);
      int count(0);
      if (! step.empty())
      {
        step.append(": ");
      }
      while(q.executeStep()) {
          std::string msg_id = q.getColumn(0);
          CPPUNIT_ASSERT_EQUAL_MESSAGE(step + "article " + msg_id ,expect.at(count++), msg_id);
      }
      CPPUNIT_ASSERT_EQUAL_MESSAGE(step + "article count" ,int(expect.size()), count);
    }

    void assert_filter_result(
      std::string label,
      std::string group,
      Data::ShowType show_type,
      std::vector<FilterResult> expect)
    {
      auto q = hf.get_sql_filter(*data, show_type, criteria);
      q.bind(q.getBindParameterCount(), group);

      int count(0);
      auto str = "article " + label;
      while (q.executeStep())
      {
        std::string msg_id = q.getColumn(0);
        bool pass = q.getColumn(1).getInt();
        auto xp = expect.at(count++);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(str + " msg_id", xp.msg_id, msg_id);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
          str + " pass of message " + msg_id, xp.pass, pass);
      }
      CPPUNIT_ASSERT_EQUAL_MESSAGE(str + " count", int(expect.size()), count);
    }

    void test_is_read()
    {
      pan_db.exec(R"SQL(
        insert into article (message_id, author_id, subject_id, time_posted, is_read)
          values ("<m1>", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234, 1);
        insert into article (message_id, author_id, subject_id, time_posted, is_read)
          values ("<m2>", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234, 0);
      )SQL");

      criteria.set_type_is_read();
      assert_result({"<m1>"});

      criteria.set_type_is_unread();
      assert_result({"<m2>"});
    }

    void test_byte_count_ge()
    {
      pan_db.exec(R"SQL(
        insert into article (message_id, author_id, subject_id, time_posted)
          values ("m1", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234);
        insert into article (message_id, author_id, subject_id, time_posted)
          values ("m2", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m1"),1,"m1p1", 1000);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m2"),1,"m2p1", 2000);
      )SQL");
      criteria.set_type_byte_count_ge(1500);

      assert_result({"m2"});
    }

    void test_byte_count_ge_or_is_read()
    {
      pan_db.exec(R"SQL(
        insert into article (message_id, author_id, subject_id, time_posted, is_read)
          values ("m1", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234,1);
        insert into article (message_id, author_id, subject_id, time_posted, is_read)
          values ("m2", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234,0);
        insert into article (message_id, author_id, subject_id, time_posted, is_read)
          values ("m3", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234,0);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m1"),1,"m1p1", 1000);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m2"),1,"m2p1", 2000);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m3"),1,"m3p1", 1200);
      )SQL");
      FilterInfo *f1 = new FilterInfo, *f2 = new FilterInfo;
      criteria.set_type_aggregate_or();
      f1->set_type_byte_count_ge(1500);
      f2->set_type_is_read();
      criteria._aggregates.push_back(f1);
      criteria._aggregates.push_back(f2);

      assert_result({"m1", "m2"});
    }

    void test_by_group_name()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      add_article("g2m3", "g2");

      TextMatch::Description d;
      d.type = TextMatch::CONTAINS;
      d.text = "g1";
      criteria.set_type_text(Quark("Xref"), d);

      assert_result({"g1m1", "g1m2"});
    }

    void test_by_nb_of_crosspost()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      add_article_in_group("g1m1", "g2");

      TextMatch::Description d;
      d.text = "(.*:){2}";
      criteria.set_type_text(Quark("Xref"), d);

      assert_result({"g1m1"});

      // test negation
      criteria.set_negate(true);
      assert_result({"g1m2"});

      d.text = ".*:.*";
    }

    void test_by_xref_test()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      add_article_in_group("g1m1", "g2");

      // test that g2 is not part of xref
      TextMatch::Description d;
      d.text = "g2";
      criteria.set_negate(true);
      criteria.set_type_text(Quark("Xref"), d);
      criteria._text._impl_type = TextMatch::CONTAINS;

      assert_result({"g1m1"});
    }

    void test_by_newsgroup()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      add_article("g2m3", "g2");
      add_article_in_group("g1m1", "g2");

      // test that g2 is not part of xref
      TextMatch::Description d;
      d.text = "g2";
      criteria.set_type_text(Quark("Newsgroups"), d);
      assert_result({"g1m1", "g2m3"});

      d.text = "g1";
      criteria.set_type_text(Quark("Newsgroups"), d);
      assert_result({"g1m1", "g1m2"});

      d.text = "g";
      criteria.set_type_text(Quark("Newsgroups"), d);
      criteria._text._impl_type = TextMatch::BEGINS_WITH;
      assert_result({"g1m1", "g1m2", "g2m3"});
    }

    void test_by_references()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        update article set `references` = "g1m1" where message_id = "g1m2"
      )SQL");

      // test that g2 is not part of xref
      TextMatch::Description d;
      d.text = "g1m1";
      criteria.set_type_text(Quark("References"), d);
      assert_result({"g1m2"});
    }

    void test_by_header()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        insert into subject (subject) values ("m1 subject"), ("m2 subject");
        update article set author_id = (select id from author where author like "Me%"),
                           subject_id = (select id from subject where subject like "m1%")
          where message_id = "g1m1";
        update article set author_id = (select id from author where author like "Other%"),
                           subject_id = (select id from subject where subject like "m2%")
          where message_id = "g1m2";
      )SQL");

      // test that g2 is not part of xref
      TextMatch::Description d;
      d.text = "m2 subject";
      criteria.set_type_text(Quark("Subject"), d);
      assert_result("subject",{"g1m2"});

      // default search is not case sensitive
      d.text = "me <me@home>";
      criteria.set_type_text(Quark("From"), d);
      assert_result("from", {"g1m1"});

      d.text = "g1m1";
      criteria.set_type_text(Quark("Message-Id"), d);
      assert_result("message id", {"g1m1"});

      d.text = "g1m1";
      criteria.set_type_text(Quark("Message-ID"), d);
      assert_result("message ID",{"g1m1"});

      criteria.set_type_posted_by_me();
      assert_result("posted by me",{"g1m1"});
    }

    void test_by_score_ge()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      add_article("g2m1", "g2");
      pan_db.exec(R"SQL(
        update article_group set score = 2
           where article_id = (select id from article where message_id == "g1m1");
        update article_group set score = 10
           where article_id = (select id from article where message_id == "g1m2");
        update article_group set score = 10
           where article_id = (select id from article where message_id == "g2m1");
      )SQL");

      criteria.set_type_score_ge(5);
      assert_result({"g1m2"});
    }

    void test_by_cache_status()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        update article set cached = True where message_id == "g1m1";
      )SQL");

      criteria.set_type_cached();
      assert_result({"g1m1"});
    }

    void test_by_line_count()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        update article set line_count = 10 where message_id == "g1m1";
        update article set line_count = 20 where message_id == "g1m2";
      )SQL");

      criteria.set_type_line_count_ge(20);
      assert_result({"g1m2"});
    }

    void test_by_days_old()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        update article set time_posted = unixepoch('now', '-10 days')
               where message_id == "g1m1";
        update article set time_posted = unixepoch('now', '-5 days')
               where message_id == "g1m2";
      )SQL");

      criteria.set_type_days_old_ge(9);
      assert_result({"g1m1"});

      criteria.set_type_days_old_ge(4);
      assert_result({"g1m1", "g1m2"});

      criteria.set_type_days_old_ge(15);
      assert_result({});
    }

    void test_by_is_binary()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        update article set part_state = 'C' where message_id == "g1m1";
      )SQL");

      criteria.set_type_binary();
      assert_result({"g1m1"});
    }

    void test_byte_count_ge_and_is_read()
    {
      pan_db.exec(R"SQL(
        insert into article (message_id,author_id, subject_id, time_posted, is_read)
          values ("m1", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234,1);
        insert into article (message_id,author_id, subject_id, time_posted, is_read)
          values ("m2", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234,1);
        insert into article (message_id,author_id, subject_id, time_posted, is_read)
          values ("m3", (select id from author where author like "Me%"),
                        (select id from subject where subject = "blah"), 1234,0);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m1"),1,"m1p1", 1000);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m2"),1,"m2p1", 2000);
        insert into article_part (article_id, part_number, part_message_id, size)
           values ((select id from article where message_id == "m3"),1,"m3p1", 1200);
      )SQL");
      FilterInfo *f1 = new FilterInfo, *f2 = new FilterInfo;
      criteria.set_type_aggregate_and();
      f1->set_type_byte_count_ge(1500);
      f2->set_type_is_read();
      criteria._aggregates.push_back(f1);
      criteria._aggregates.push_back(f2);

      assert_result({"m2"});
    }

    void test_single_article()
    {
      add_article("g1m1", "g1");
      add_article("g1m2", "g1");
      pan_db.exec(R"SQL(
        update article set part_state = 'C' where message_id == "g1m1";
      )SQL");

      criteria.set_type_binary();

      SqlCond precond("message_id = ? and", "g1m1");
      auto q = hf.get_sql_query(*data, "count()", precond, criteria);
      while (q.executeStep())
      {
        int count = q.getColumn(0);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("is g1m1 binary", 1, count);
      }

      precond.str_param = "g2m2";
      q = hf.get_sql_query(*data, "count()", precond, criteria);
      while (q.executeStep())
      {
        int count = q.getColumn(0);
        CPPUNIT_ASSERT_EQUAL_MESSAGE("is g2m2 binary", 0, count);
      }
    }

    void test_article_filter()
    {
      add_article("g1m1", "g1");
      pan_db.exec(R"SQL(
        update article set part_state = 'C' where message_id == "g1m1";
      )SQL");

      add_article("g1m2", "g1");
      add_article("g2m1", "g2");

      criteria.set_type_binary();

      assert_filter_result("plain filter",
                           "g1",
                           pan::Data::SHOW_ARTICLES,
                           {{"g1m1", true}, {"g1m2", false}});
    }

    void test_article_filter_with_thread()
    {
      // g1m1a -> g1m1b, g1m1b2 => g1m1c1, g1m1c2
      add_article("g1m1a", "g1");
      add_article("g1m1b", "g1");
      data->store_references("g1m1b", "g1m1a"); // add ancestor
      add_article("g1m1b2", "g1");
      data->store_references("g1m1b2", "g1m1a"); // add ancestor
      add_article("g1m1c1", "g1");
      data->store_references("g1m1c1", "g1m1a g1m1b"); // add ancestors
      add_article("g1m1c2", "g1");
      data->store_references("g1m1c2", "g1m1a g1m1b"); // add ancestors

      // g1m2a -> g1m2b -> g1m2c
      add_article("g1m2a", "g1");
      add_article("g1m2b", "g1");
      data->store_references("g1m2b", "g1m2a"); // add ancestor
      add_article("g1m2c", "g1");
      data->store_references("g1m2c", "g1m2a g1m2b"); // add ancestors

      add_article("g1m2", "g1"); // no ancestors
      add_article("g2m1", "g2");
      pan_db.exec(R"SQL(
        update article set part_state = 'C' where message_id == "g1m1b";
        update article set part_state = 'I' where message_id != "g1m1b";
      )SQL");

      criteria.set_type_binary();
      // get passing articles and their ancestors
      assert_filter_result("thread filter (parent and children)",
                           "g1",
                           pan::Data::SHOW_THREADS,
                           {
                             {"g1m1a", true},
                             {"g1m1b", true},
                             {"g1m1b2", false},
                             {"g1m1c1", true},
                             {"g1m1c2", true},
                             {"g1m2", false},
                             {"g1m2a", false},
                             {"g1m2b", false},
                             {"g1m2c", false},
                           });
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_is_read);
    CPPUNIT_TEST(test_byte_count_ge);
    CPPUNIT_TEST(test_byte_count_ge_or_is_read);
    CPPUNIT_TEST(test_by_group_name);
    CPPUNIT_TEST(test_by_nb_of_crosspost);
    CPPUNIT_TEST(test_by_xref_test);
    CPPUNIT_TEST(test_by_newsgroup);
    CPPUNIT_TEST(test_by_references);
    CPPUNIT_TEST(test_by_header);
    CPPUNIT_TEST(test_by_score_ge);
    CPPUNIT_TEST(test_by_cache_status);
    CPPUNIT_TEST(test_by_line_count);
    CPPUNIT_TEST(test_by_days_old);
    CPPUNIT_TEST(test_by_is_binary);
    CPPUNIT_TEST(test_byte_count_ge_and_is_read);
    CPPUNIT_TEST(test_single_article);
    CPPUNIT_TEST(test_article_filter);
    CPPUNIT_TEST(test_article_filter_with_thread);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
