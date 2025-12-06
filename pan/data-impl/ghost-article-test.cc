#include "pan/data-impl/data-impl.h"
#include "pan/general/string-view.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <config.h>
#include <numeric>
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

char const *db_file("/tmp/data-impl-article.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

class DataImplTest : public CppUnit::TestFixture
{
private:
    DataImpl *data;

    struct CheckRef {
        std::string msg_id;
        std::string parent_msg_id;
        std::string ghost_msg_id;
        std::string ghost_parent_msg_id;
    };

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
         delete from ghost;
         delete from article;
         delete from server_group;
         delete from `group`;
         delete from author;
         delete from subject;
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

    void assert_no_ghost_article() {
        std::string str("select ghost_msg_id from ghost");
        SQLite::Statement q(pan_db, str);
        std::vector<std::string> found;
        while (q.executeStep()) {
            std::string ghost (q.getColumn(0));
            found.push_back(std::string(" ") + ghost);
        }
        auto warn = std::string("failed ghost article check, got");
        auto msg(std::accumulate(found.begin(), found.end(), warn));
        CPPUNIT_ASSERT_MESSAGE(msg, found.empty());
    }

    void check_ghost_presence(std::string msg_id,
                            bool present)
    {
        SQLite::Statement query(pan_db, R"SQL(
          select count() from ghost where ghost_msg_id = ?
        )SQL");
        query.bind(1, msg_id);

        while(query.executeStep()) {
            bool res = query.getColumn(0).getInt() > 0;
            CPPUNIT_ASSERT_EQUAL_MESSAGE("ghost " + msg_id + " presence", present, res);
        }
    }

    void check_ghost_tree(std::string label,
                        std::string test_msg_id,
                        CheckRef expect)
    {
      SQLite::Statement query(pan_db, R"SQL(
            select a.message_id,
                   (select message_id from article where id == a.parent_id) as parent_msg_id,
                   g.ghost_msg_id,
                   g.ghost_parent_msg_id
            from article as a
            left outer join ghost as g on a.ghost_parent_id == g.id
            where a.message_id = ?
            order by g.ghost_parent_msg_id
        )SQL");

      std::string msg_id, parent_id;
      query.bind(1, test_msg_id);
      while (query.executeStep())
      {
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + ": msg_id",
                                     expect.msg_id,
                                     std::string(query.getColumn(0).getText()));
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + ": parent msg id",
                                     expect.parent_msg_id,
                                     std::string(query.getColumn(1).getText()));
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + ": ghost msg id",
                                     expect.ghost_msg_id,
                                     std::string(query.getColumn(2).getText()));
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + ": ghost parent msg id",
                                     expect.ghost_parent_msg_id,
                                     std::string(query.getColumn(3).getText()));
      }
    }

    void add_article(std::string msg_id, std::string references)
    {
      SQLite::Statement setup(pan_db, R"SQL(
        insert into article (message_id,author_id, subject_id, time_posted)
          values (?, (select id from author where author like "Me%"),
                     (select id from subject where subject = "blah"), 1234)
      )SQL");
      setup.bind(1, msg_id);
      int res (setup.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article " + msg_id, 1, res);
      data->store_references(msg_id, references);
    }

    void test_normal_insertion()
    {
      // create an article tree from ancestor to child: a1 -> a2 -> a3
      // created with normal order. i.e. a1 a2 a3
      // no ghost article is created here
      std::string a1("<a1>"), a2("<a2>"), a3("<a3>");

      add_article(a1, "");
      check_ghost_tree("a1 only", a1, {a1});

      add_article(a2, a1);
      check_ghost_tree("a2->a1", a1, {a1});
      check_ghost_tree("a2->a1", a2, {a2, a1, "", ""});

      add_article(a3, a1 + " " + a2);
      check_ghost_tree("a3->a1", a1, {a1});
      check_ghost_tree("a3->a2", a2, {a2, a1, "", ""});
      check_ghost_tree("a3->a3", a3, {a3, a2, "", ""});
    }

    void test_reverse_insertion()
    {
      // create an article tree from ancestor to child: a1 -> a2 -> a3
      // created in that order: a3 a1 a2
      // no ghost article is created here
      std::string a1("<a1>"), a2("<a2>"), a3("<a3>");

      // parent's reference field followed by parent's message_id,
      // with a redundant ref
      add_article(a3, a1 + " " + a2 + " " + a3);
      check_ghost_presence(a1, true);
      check_ghost_presence(a2, true);
      check_ghost_tree("a3", a3, {a3, "", a2, a1});
      check_ghost_tree("a3->a2", a2, {a2, "", a1, ""});
      check_ghost_tree("a3->a1", a1, {a1, "", "", ""});

      add_article(a1, "");
      check_ghost_presence(a1, false);
      check_ghost_presence(a2, true);
      check_ghost_tree("a1->a3", a3, {a3, a1, a2, a1});
      check_ghost_tree("a1->a1", a1, {a1});

      add_article(a2, a1);
      check_ghost_presence(a2,false);
      check_ghost_tree("a2->a3", a3, {a3, a2, "", ""});
      check_ghost_tree("a2->a2", a2, {a2, a1, "", ""});
      check_ghost_tree("a2->a1", a1, {a1});
    }

    void test_complex_tree()
    {
      // create a more complex tree. -> means parent to child
      // b1 -> d2 -> d3 -> d4
      //   \-> b2 -> c3
      //         \-> b3

      std::string b1("<b1>"), b2("<b2>"), b3("<b3>"), c3("<c3>"), d2("<d2>"),
          d3("<d3>"), d4("<d4>");

      add_article(b3, b1 + " " + b2);
      check_ghost_tree("b3", b3, {b3, "", b2, b1});

      add_article(c3, b1 + " " + b2);
      check_ghost_tree("c3", c3, {c3, "", b2, b1});

      add_article(b2, b1);
      check_ghost_presence(b2,false);
      check_ghost_tree("b2->b2", b2, {b2, "", b1, ""});
      check_ghost_tree("b2->b3", b3, {b3, b2, "", ""});
      check_ghost_tree("b2->c3", c3, {c3, b2, "", ""});

      add_article(b1, "");
      check_ghost_presence(b1,false);
      check_ghost_tree("b1->b2", b2, {b2, b1, "", ""});
      check_ghost_tree("b1", b1, {b1});

      add_article(d4, b1 + " " + d2 + " " + d3);
      check_ghost_presence(b1,false);
      check_ghost_tree("d4", d4, {d4,b1,d3,d2});
      check_ghost_tree("d4->b2", b2, {b2, b1, "", ""});
      check_ghost_tree("d4->b1", b1, {b1});
      check_ghost_tree("d4->c3", c3, {c3, b2, "", ""});

      add_article(d3, b1 + " " + d2);
      check_ghost_presence(d3,false);
      check_ghost_tree("d3", d3, {d3,b1,d2,b1});

      add_article(d2, b1);
      check_ghost_presence(d2,false);
      check_ghost_tree("d2", d2, {d2,b1,"",""});
    }
    // seen in the wild: references from sibling lead to a circular reference
    void test_circular_references() {
      // g1m1a -> g1m1b +--> g1m1c1 -> g1m1d1
      //                 \-> g1m1c2 -> g1m1d2
      add_article("g1m1a", "");
      add_article("g1m1b", "g1m1a");
      add_article("g1m1c1", "g1m1a g1m1b");
      add_article("g1m1c2", "g1m1a g1m1b");
      add_article("g1m1d1", "g1m1a g1m1b g1m1c1");
      // add ancestors and lie about one ancestor
      add_article("g1m1d2", "g1m1d1 g1m1a g1m1b g1m1c2");

      // the above lie is discarded because g1m1a was already in DB
      check_ghost_presence("g1m1d1", false);

      assert_no_ghost_article();
    }

    void test_circular_references_reverse_order() {
      // g1m1a -> g1m1b +--> g1m1c1 -> g1m1d1
      //                 \-> g1m1c2 -> g1m1d2

      // add ghost ancestors and lie about one ancestor
      add_article("g1m1d2", "g1m1d1 g1m1a g1m1b g1m1c2");
      check_ghost_presence("g1m1d1", true);

      add_article("g1m1a", ""); // clobber g1m1a ghost article

      add_article("g1m1b", "g1m1a");
      add_article("g1m1c1", "g1m1a g1m1b");
      add_article("g1m1c2", "g1m1a g1m1b");
      add_article("g1m1d1", "g1m1a g1m1b g1m1c1");

      // the lie is discarded because g1m1a was already in DB
      check_ghost_presence("g1m1d1", false);

      // check that ghost articles are gone
      assert_no_ghost_article();
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_normal_insertion);
    CPPUNIT_TEST(test_reverse_insertion);
    CPPUNIT_TEST(test_complex_tree);
    CPPUNIT_TEST(test_circular_references);
    CPPUNIT_TEST(test_circular_references_reverse_order);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
