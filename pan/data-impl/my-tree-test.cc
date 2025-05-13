#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data/data.h"
#include "pan/general/quark.h"
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

    struct ExpArticle
    {
        std::string msg_id;
        bool has_child;
    };

  public:
    void setUp()
    {
      StringView cache;
      prefs = new PrefsFile("/tmp/dummy.xml");
      // cleanup whatever may be left from previous test runs
      load_db_schema(pan_db);
      std::cout << "cleanup called\n";
      pan_db.exec(R"SQL(
         delete from article_part;
         delete from article_xref;
         delete from article_group;
         delete from exposed_article;
         delete from hidden_article;
         delete from reparented_article;
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

      // g1m1a -> g1m1b => g1m1c1, g1m1c2 => g1m1d1, g1m1d2
      add_article("g1m1a", "g1");
      add_article("g1m1b", "g1");
      data->store_references("g1m1b", "g1m1a"); // add ancestor
      add_article("g1m1c1", "g1");
      data->store_references("g1m1c1", "g1m1a g1m1b"); // add ancestors
      add_article("g1m1c2", "g1");
      data->store_references("g1m1c2", "g1m1a g1m1b"); // add ancestors
      add_article("g1m1d1", "g1");
      data->store_references("g1m1d1", "g1m1a g1m1b g1m1c1"); // add ancestors
      add_article("g1m1d2", "g1");
      data->store_references("g1m1d2", "g1m1a g1m1b g1m1c2"); // add ancestors

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
      int res(q_article.exec());
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

    void assert_result(Quark const &mid,
                       Quark const &group,
                       std::vector<ExpArticle> expect)
    {
      std::vector<pan::Data::ArticleTree::ArticleChild> setme;

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
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " child msg_id ",
                                     expect[i].msg_id,
                                     setme[i].a.message_id.to_string());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(label + "/" + expect[i].msg_id
                                       + " has child ",
                                     expect[i].has_child,
                                     setme[i].has_child);
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        label + " children count", int(expect.size()), int(setme.size()));
    }

    void assert_hidden(std::string msg_id, bool expect) {
        assert_prop("hidden", msg_id, expect);
    }

    void assert_exposed(std::string msg_id, bool expect) {
        assert_prop("exposed", msg_id, expect);
    }

    void assert_prop(std::string prop, std::string msg_id, bool expect) {
      SQLite::Statement q(pan_db,
                          "select count() from " + prop + "_article"
                            + " join article on article.id == "
                              "hidden_article.article_id where message_id = ?");
      q.bind(1, msg_id);

      int count(0);
      while(q.executeStep()) {
          count += q.getColumn(0).getInt();
      }

      CPPUNIT_ASSERT_MESSAGE("msg_id " + msg_id + " " + prop + " consistency", count <= 1);
      CPPUNIT_ASSERT_EQUAL_MESSAGE("msg_id " + msg_id + " is " + prop, expect, count == 1);
    };

    void change_read_status(Quark const mid, bool status)
    {
      SQLite::Statement q(pan_db, R"SQL(
        update article set is_read = ? where message_id = ?
      )SQL");
      q.bind(1, status);
      q.bind(2, mid);
      q.exec();
    }

    // inspect content of article_view and message_id with
    // select message_id, article_id, is_read, av.parent_id, has_child, show
    // from article join article_view as av on av.article_id = article.id

    // emulates showing all articles
    void test_get_children()
    {
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES);
      tree->initialize_article_view();
      assert_result(
        Quark(), "g1", {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
      assert_result("g1m1a", "g1", {{"g1m1b", true}});
      assert_result("g1m1b", "g1", {{"g1m1c1", true}, {"g1m1c2", true}});
      assert_result("g1m1c1", "g1", {{"g1m1d1", false}});
      assert_result("g1m1c2", "g1", {{"g1m1d2", false}});
    }

    // emulates showing unread articles with 2 read article in the beginning of
    // a thread
    void test_get_unread_children_beginning_thread()
    {
      // start test with 2 read articles
      change_read_status("g1m1a", true);
      change_read_status("g1m1b", true);

      // init article view
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      std::vector<ExpArticle> original_roots(
        {{"g1m1c1", true}, {"g1m1c2", true}, {"g1m2a", true}, {"g1m2", false}});

      assert_result(Quark(), "g1", original_roots);

      // the read articles (g1m1b and g1m1c1) are no longer in tree
      assert_result("g1m1c1", "g1", {{"g1m1d1", false}});
      assert_result("g1m1c2", "g1", {{"g1m1d2", false}});

      // now read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // new articles in root
      assert_result(Quark(),
                    "g1",
                    {{"g1m1c1", true},
                     {"g1m1d2", false},
                     {"g1m2a", true},
                     {"g1m2", false}});

      // check content of hidden article
      assert_hidden("g1m1c1", false);
      assert_hidden("g1m1c2", true);

      // TODO: what should reparented value be ?

      // mark article  as unread
      change_read_status("g1m1c2", false);
      tree->update_article_view();
      // check content of added article and reparented ?
      assert_result(Quark(), "g1", original_roots);
      assert_hidden("g1m1c2", false);
    }

    // emulates showing unread articles with 2 read article in the middle of a thread
    void test_get_unread_children_midddle_thread ()
    {
      // start test with 2 read articles
      change_read_status("g1m1b", true);
      change_read_status("g1m1c1", true);

      // init article view
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      assert_result(
        Quark(), "g1", {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
      // the read articles (g1m1b and g1m1c1) are no longer in tree
      assert_result("g1m1a", "g1", {{"g1m1c2", true}, {"g1m1d1", false}});

      // now read another article
    }

    // emulates showing unread articles with 2 read article in the end of a thread
    void test_get_unread_children_end_thread ()
    {
      change_read_status("g1m1c1", true);
      change_read_status("g1m1d1", true);
      change_read_status("g1m1d2", true);

      // init article view
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      assert_result(
        Quark(), "g1", {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
      // the read articles (g1m1b and g1m1c1) are no longer in tree
      assert_result("g1m1a", "g1", {{"g1m1b", true}});
      assert_result("g1m1b", "g1", {{"g1m1c2", false}});
      // change_read_status("g1m1c2", true);
      // tree->update_article_view();
      // assert_result("g1m1b after reading g1m1c2", "g1", {});
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_get_children);
    CPPUNIT_TEST(test_get_unread_children_beginning_thread);
    CPPUNIT_TEST(test_get_unread_children_midddle_thread);
    CPPUNIT_TEST(test_get_unread_children_end_thread);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(DataImplTest::suite());
  bool wasSucessful = runner.run();
  return wasSucessful ? 0 : 1;
}
