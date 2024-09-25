#include "pan/data/data.h"
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
#include <pan/gui/group-prefs.h>
#include <pan/gui/prefs-file.h>
#include <pan/usenet-utils/filter-info.h>
#include <string>

#include <cppunit/TestAssert.h>
#include <cppunit/TestResult.h>
#include <cppunit/ui/text/TestRunner.h>
#include <cppunit/extensions/HelperMacros.h>

using namespace pan;

class DataImplTest : public CppUnit::TestFixture
{
private:
    SQLiteDb *my_db;
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
      // always start from an empty db
      char const *db_file("/tmp/data-impl.db");
      std::remove(db_file);
      my_db = new SQLiteDb(db_file, SQLite::OPEN_READWRITE|SQLite::OPEN_CREATE);
      std::cout << "Creating DB";

      StringView cache;
      Prefs prefs;
      data = new DataImpl(cache, prefs, *my_db);
      load_db_schema(*my_db);
      my_db->exec("insert into author (author) values (\"me\")");
    }

    void tearDown()
    {
    }

    void checkGhostPresence(std::string msg_id,
                            bool present)
    {
        SQLite::Statement query(*my_db, R"SQL(
          select count() from ghost where ghost_msg_id = ?
        )SQL");
        query.bind(1, msg_id);

        while(query.executeStep()) {
            bool res = query.getColumn(0).getInt() > 0;
            CPPUNIT_ASSERT_EQUAL_MESSAGE("ghost " + msg_id + " presence", present, res);
        }
    }

    void checkGhostTree(std::string label,
                        std::string test_msg_id,
                        CheckRef expect)
    {
      SQLite::Statement query(*my_db, R"SQL(
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

    void addArticle(std::string msg_id, std::string references)
    {
      SQLite::Statement setup(*my_db, R"SQL(
        insert into article (message_id,subject,author_id, time_posted) values (?, "plop",1, 1234)
      )SQL");
      setup.bind(1, msg_id);
      setup.exec();
      data->store_references(msg_id, references);
    }

    void test_normal_insertion()
    {
      // create an article tree from ancestor to child: a1 -> a2 -> a3
      // created with normal order. i.e. a1 a2 a3
      // no ghost article is created here
      std::string a1("<a1>"), a2("<a2>"), a3("<a3>");

      addArticle(a1, "");
      checkGhostTree("a1 only", a1, {a1});

      addArticle(a2, a1);
      checkGhostTree("a2->a1", a1, {a1});
      checkGhostTree("a2->a1", a2, {a2, a1, "", ""});

      addArticle(a3, a1 + " " + a2);
      checkGhostTree("a3->a1", a1, {a1});
      checkGhostTree("a3->a2", a2, {a2, a1, "", ""});
      checkGhostTree("a3->a3", a3, {a3, a2, "", ""});
    }

    void test_reverse_insertion()
    {
      // create an article tree from ancestor to child: a1 -> a2 -> a3
      // created in that order: a3 a1 a2
      // no ghost article is created here
      std::string a1("<a1>"), a2("<a2>"), a3("<a3>");

      // parent's reference field followed by parent's message_id,
      // with a redundant ref
      addArticle(a3, a1 + " " + a2 + " " + a3);
      checkGhostPresence(a1, true);
      checkGhostPresence(a2, true);
      checkGhostTree("a3", a3, {a3, "", a2, a1});
      checkGhostTree("a3->a2", a2, {a2, "", a1, ""});
      checkGhostTree("a3->a1", a1, {a1, "", "", ""});

      addArticle(a1, "");
      checkGhostPresence(a1, false);
      checkGhostPresence(a2, true);
      checkGhostTree("a1->a3", a3, {a3, a1, a2, a1});
      checkGhostTree("a1->a1", a1, {a1});

      addArticle(a2, a1);
      checkGhostPresence(a2,false);
      checkGhostTree("a2->a3", a3, {a3, a2, "", ""});
      checkGhostTree("a2->a2", a2, {a2, a1, "", ""});
      checkGhostTree("a2->a1", a1, {a1});
    }

    void test_complex_tree()
    {
      // create a more complex tree. -> means parent to child
      // b1 -> d2 -> d3 -> d4
      //   \-> b2 -> c3
      //         \-> b3

      std::string b1("<b1>"), b2("<b2>"), b3("<b3>"), c3("<c3>"), d2("<d2>"),
          d3("<d3>"), d4("<d4>");

      addArticle(b3, b1 + " " + b2);
      checkGhostTree("b3", b3, {b3, "", b2, b1});

      addArticle(c3, b1 + " " + b2);
      checkGhostTree("c3", c3, {c3, "", b2, b1});

      addArticle(b2, b1);
      checkGhostPresence(b2,false);
      checkGhostTree("b2->b2", b2, {b2, "", b1, ""});
      checkGhostTree("b2->b3", b3, {b3, b2, "", ""});
      checkGhostTree("b2->c3", c3, {c3, b2, "", ""});

      addArticle(b1, "");
      checkGhostPresence(b1,false);
      checkGhostTree("b1->b2", b2, {b2, b1, "", ""});
      checkGhostTree("b1", b1, {b1});

      addArticle(d4, b1 + " " + d2 + " " + d3);
      checkGhostPresence(b1,false);
      checkGhostTree("d4", d4, {d4,b1,d3,d2});
      checkGhostTree("d4->b2", b2, {b2, b1, "", ""});
      checkGhostTree("d4->b1", b1, {b1});
      checkGhostTree("d4->c3", c3, {c3, b2, "", ""});

      addArticle(d3, b1 + " " + d2);
      checkGhostPresence(d3,false);
      checkGhostTree("d3", d3, {d3,b1,d2,b1});

      addArticle(d2, b1);
      checkGhostPresence(d2,false);
      checkGhostTree("d2", d2, {d2,b1,"",""});
    }
    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_normal_insertion);
    CPPUNIT_TEST(test_reverse_insertion);
    CPPUNIT_TEST(test_complex_tree);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  runner.run();
  return 0;
}
