#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data/data.h"
#include "pan/general/string-view.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <config.h>
#include <cstdio>
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
#include <vector>

using namespace pan;

char const *db_file("/tmp/data-impl-my-tree.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

class DataImplTest : public CppUnit::TestFixture
{
private:
    DataImpl *data;
    HeaderFilter hf;
    FilterInfo criteria;
    Data::ArticleTree *tree;
    PrefsFile *prefs;

public:
    void setUp()
    {
      StringView cache;
      prefs = new PrefsFile ("/tmp/dummy.xml");
      // cleanup whatever may be left from previous test runs
      load_db_schema(pan_db);
      std::cout << "cleanup called\n" ;
      pan_db.exec(R"SQL(
         delete from article_part;
         delete from article_xref;
         delete from article_group;
         delete from article;
      )SQL");
      data = new DataImpl(cache, *prefs);
      pan_db.exec(R"SQL(
        insert into author (name, address)
               values ("Me","me@home"),("Other", "other@home")
          on conflict do nothing;
        insert into server (host, port, pan_id, newsrc_filename)
                    values ("dummy", 2, 1, "/dev/null") on conflict do nothing;
        insert into `group` (name, migrated) values ("g1", 1),("g2",1) on conflict do nothing;
        insert into profile (name, author_id)
                    values ("my_profile",1) on conflict do nothing;
      )SQL");
      criteria.clear();
      // That's dumb but that how it's done in header-pane.cc.
      criteria.set_type_aggregate_and();

      // g1m1a -> g1m1b => g1m1c1, g1m1c2
      add_article("g1m1a", "g1");
      add_article("g1m1b", "g1");
      data->store_references("g1m1b", "g1m1a"); // add ancestor
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
    }

    void tearDown()
    {
    }

    void add_article(std::string msg_id)
    {
      SQLite::Statement q_article(pan_db, R"SQL(
        insert into article (message_id,author_id, time_posted) values (?, 1, 1234);
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

    void assert_result(Quark const &mid, Quark const &group, std::vector<std::string> expect)
    {
      std::vector<Article> setme;
      if (mid.empty())
      {
        tree = data->group_get_articles(group, "/tmp", Data::SHOW_ARTICLES);
      }

      // get root chidren
      tree->get_children_sql(mid, group, setme);

      std::sort(setme.begin(),
                setme.end(),
                [](pan::Data::ArticleTree::ArticleChild a,
                   pan::Data::ArticleTree::ArticleChild b)
                {
                  return a.a.message_id.to_string().compare(b.a.message_id);
                });
      std::sort(expect.begin(),
                expect.end(),
                [](ExpArticle a, ExpArticle b)
                {
                  return a.msg_id.compare(b.msg_id);
                });

      std::string label(mid.empty() ? "root" : mid.to_string());

      for (int i(0); i < expect.max_size() && i < setme.size(); i++)
      {
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
          label + " child msg_id ", expect[i], setme[i].message_id.to_string());
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        label + " children count", int(expect.size()), int(setme.size()));
    }

    void test_get_children()
    {
      assert_result(Quark(), "g1", {"g1m1a", "g1m2a", "g1m2"});
      assert_result("g1m1a", "g1", {"g1m1b"});
      assert_result("g1m1b", "g1", {"g1m1c1", "g1m1c2"});
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_get_children);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  return runner.run();
}
