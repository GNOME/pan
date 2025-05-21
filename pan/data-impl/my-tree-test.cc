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
#include <unordered_map>
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

    void assert_result(std::string label,
                       Quark const &mid,
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
                  return a.a.message_id.to_string() < b.a.message_id.to_string();
                });
      std::sort(expect.begin(),
                expect.end(),
                [](ExpArticle a, ExpArticle b)
                {
                  return a.msg_id < b.msg_id;
                });

      std::string user_message(label + " "
                               + (mid.empty() ? "root" : mid.to_string()));

      for (int i(0); i < expect.max_size() && i < setme.size(); i++)
      {
        CPPUNIT_ASSERT_EQUAL_MESSAGE(user_message + " child msg_id ",
                                     expect[i].msg_id,
                                     setme[i].a.message_id.to_string());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(user_message + "/" + expect[i].msg_id
                                       + " has child ",
                                     expect[i].has_child,
                                     setme[i].has_child);
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(user_message + " children count",
                                   int(expect.size()),
                                   int(setme.size()));
    }

    void assert_hidden(std::string label, std::string msg_id, bool expect)
    {
      assert_prop(label, "hidden", msg_id, expect);
    }

    void assert_exposed(std::string label, std::string msg_id, bool expect)
    {
      assert_prop(label, "exposed", msg_id, expect);
    }

    void assert_reparented(std::string label, std::string msg_id, bool expect)
    {
      assert_prop(label, "reparented", msg_id, expect);
    }

    void assert_prop(std::string label,
                     std::string prop,
                     std::string msg_id,
                     bool expect)
    {
      std::string table_name(prop + "_article");
      SQLite::Statement q(pan_db,
                          "select count() from " + table_name
                            + " join article on article.id == " + table_name
                            + ".article_id where message_id = ?");
      q.bind(1, msg_id);

      int count(0);
      while (q.executeStep())
      {
        count += q.getColumn(0).getInt();
      }

      CPPUNIT_ASSERT_MESSAGE(
        label + " msg_id " + msg_id + " " + prop + " consistency", count <= 1);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        label + " msg_id " + msg_id + " is " + prop, expect, count == 1);
    };

    void change_read_status(Quark const mid, bool status)
    {
      SQLite::Statement q(pan_db, R"SQL(
        update article set is_read = ? where message_id = ?
      )SQL");
      q.bind(1, status);
      q.bind(2, mid);
      int count = q.exec();
      CPPUNIT_ASSERT_MESSAGE("change read status msg_id " + mid.to_string(), count == 1);
    }

    // inspect content of article_view and message_id with
    // select message_id, article_id, is_read, av.parent_id, has_child, show
    // from article join article_view as av on av.article_id = article.id

    // emulates showing all articles, with no criteria
    void test_get_children()
    {
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES);
      tree->initialize_article_view();
      assert_result(
        "", Quark(), "g1", {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
      assert_result("", "g1m1a", "g1", {{"g1m1b", true}});
      assert_result("", "g1m1b", "g1", {{"g1m1c1", true}, {"g1m1c2", true}});
      assert_result("", "g1m1c1", "g1", {{"g1m1d1", false}});
      assert_result("", "g1m1c2", "g1", {{"g1m1d2", false}});
    }

    // emulates showing all articles, with no criteria
    void test_get_children_with_empty_criteria()
    {
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      assert_result(
        "", Quark(), "g1", {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
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

      assert_result("step 1", Quark(), "g1", original_roots);

      // the read articles (g1m1b and g1m1c1) are no longer in tree
      assert_result("step 1", "g1m1c1", "g1", {{"g1m1d1", false}});
      assert_result("step 1", "g1m1c2", "g1", {{"g1m1d2", false}});

      // now read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // new articles in root
      assert_result("step 2",
                    Quark(),
                    "g1",
                    {{"g1m1c1", true},
                     {"g1m1d2", false},
                     {"g1m2a", true},
                     {"g1m2", false}});

      // check content of hidden article
      assert_hidden("step 2", "g1m1c1", false);
      assert_hidden("step 2", "g1m1c2", true);

      // mark article  as unread
      change_read_status("g1m1c2", false);
      tree->update_article_view();
      // check content of added article and reparented ?
      assert_result("step 3", Quark(), "g1", original_roots);
      assert_hidden("step 3", "g1m1c2", false);
    }

    // emulates showing unread articles with 2 read article in the middle of a
    // thread
    void test_get_unread_children_midddle_thread()
    {
      // start test with 2 read articles
      change_read_status("g1m1b", true);
      change_read_status("g1m1c1", true);

      // init article view
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      assert_reparented("step 1", "g1m1d1", false);
      assert_reparented("step 1", "g1m1c2", false);

      std::vector<ExpArticle> original_roots(
        {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
      assert_result("step 1", Quark(), "g1", original_roots);
      // the read articles (g1m1b and g1m1c1) are no longer in tree
      assert_result(
        "step 1", "g1m1a", "g1", {{"g1m1c2", true}, {"g1m1d1", false}});

      // now read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // no change in root
      assert_result("step 2", Quark(), "g1", original_roots);
      // g1m1c2 is removed from tree, new child is g1m1d2
      assert_result(
        "step 2", "g1m1a", "g1", {{"g1m1d1", false}, {"g1m1d2", false}});
      assert_hidden("step 2", "g1m1c2", true);
      // hidden articles should not be flagged as reparented
      assert_reparented("step 2", "g1m1c2", false);
      assert_reparented("step 2", "g1m1d1", false);
      assert_reparented("step 2", "g1m1d2", true);

      // mark an article as unread
      change_read_status("g1m1c1", false);
      tree->update_article_view();
      // no change in root
      assert_result("step 3", Quark(), "g1", original_roots);
      // g1m1c1 is back in tree
      assert_result(
        "step 3", "g1m1a", "g1", {{"g1m1c1", true}, {"g1m1d2", false}});
      assert_hidden("step 3", "g1m1c2", false);
      assert_exposed("step 3", "g1m1c1", true);
      // hidden articles should not be flagged as reparented
      assert_reparented("step 3", "g1m1c2", false);
      assert_reparented("step 3", "g1m1d1", true);
      assert_reparented("step 3", "g1m1d2", false);
    }

    // emulates showing unread articles with 2 read article in the end of a
    // thread
    void test_get_unread_children_end_thread()
    {
      change_read_status("g1m1c1", true);
      change_read_status("g1m1d1", true);
      change_read_status("g1m1d2", true);

      // init article view
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      std::vector<ExpArticle> original_roots(
        {{"g1m1a", true}, {"g1m2a", true}, {"g1m2", false}});
      assert_result("step 1", Quark(), "g1", original_roots);

      // the read articles are no longer in tree
      assert_result("step 1", "g1m1a", "g1", {{"g1m1b", true}});
      assert_result("step 1", "g1m1b", "g1", {{"g1m1c2", false}});

      // now read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // no change in root
      assert_result("step 2", Quark(), "g1", original_roots);

      // g1m1b has no child
      assert_result("step 2", "g1m1a", "g1", {{"g1m1b", false}});
      assert_hidden("step 2", "g1m1c2", true);
      // hidden articles should not be flagged as reparented
      assert_reparented("step 2", "g1m1c2", false);
      assert_reparented("step 2", "g1m1d1", false);
      assert_reparented("step 2", "g1m1d2", false);

      // mark an article as unread
      change_read_status("g1m1c1", false);
      tree->update_article_view();
      // no change in root
      assert_result("step 3", Quark(), "g1", original_roots);
      assert_result("step 3", "g1m1a", "g1", {{"g1m1b", true}});
      // g1m1c1 is back in tree
      assert_result("step 3", "g1m1b", "g1", {{"g1m1c1", false}});
      assert_hidden("step 3", "g1m1c2", false);
      assert_exposed("step 3", "g1m1c1", true);
      // exposed articles should not be flagged as reparented
      assert_reparented("step 3", "g1m1c1", false);
      assert_reparented("step 3", "g1m1c2", false);
      assert_reparented("step 3", "g1m1d1", false);
      assert_reparented("step 3", "g1m1d2", false);
    }

    // emulates showing article using a callback, and with articles
    // sorted in hierarchy
    void test_function_on_exposed_articles()
    {
      change_read_status("g1m2", true);
      change_read_status("g1m2a", true);
      change_read_status("g1m1c1", true);

      // init article view
      criteria.set_type_is_read();
      tree =
          data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      // we can view the 3 read articles. They are in separate threads
      // so they have no children
      assert_result("step 1",
                    Quark(),
                    "g1",
                    {{"g1m1c1", false}, {"g1m2", false}, {"g1m2a", false}});

      // change filter
      criteria.set_type_is_unread();
      tree->set_filter(Data::SHOW_ARTICLES, &criteria);
      tree->update_article_view();

      assert_result(
        "step 2", Quark(), "g1", {{"g1m1a", true}, {"g1m2b", true}});

      // function that checks that parent_id is either null or already
      // provided.
      std::set<std::string> stack;
      auto check
      = [&stack](Quark msg_id, Quark prt_id)
      {
        // std::cout << "check " << msg_id << " parent " << prt_id << "\n";
        if (! prt_id.empty())
        {
          auto search = stack.find(prt_id.to_string());
          CPPUNIT_ASSERT_MESSAGE("step 3 msg_id " + msg_id.to_string()
                                   + " parent_id " + prt_id.to_string()
                                   + " consistency",
                                 search != stack.end());
        }
        stack.insert(msg_id.to_string());
      };

      int count = tree->call_on_exposed_articles(check);
      CPPUNIT_ASSERT_EQUAL_MESSAGE("step 3 exposed count ", 7, count);
    }

    // check reparented status of article.
    void test_function_on_reparented_articles()
    {
      // init article view, some articles are shown
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      // change status and re-apply filter, some articles are hidden
      // and their children are reparented.
      change_read_status("g1m1c1", true);
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      assert_reparented("step 2", "g1m1d1", true);
      assert_reparented("step 2", "g1m1d2", true);

      // msg_id -> new_parent_id
      std::unordered_map<std::string, std::string> reparented;
      auto insert_in_stack = [&reparented](Quark msg_id, Quark prt_id)
      {
        // std::cout << "check reparented " << msg_id << "\n";
        reparented.insert({msg_id.to_string(), prt_id.to_string()});
      };
      int count = tree->call_on_reparented_articles(insert_in_stack);

      CPPUNIT_ASSERT_EQUAL_MESSAGE("check reparented nb", 2 , count);
      CPPUNIT_ASSERT_MESSAGE("check reparented g1m1d1",
                             reparented.find("g1m1d1") != reparented.end());
      CPPUNIT_ASSERT_MESSAGE("check reparented g1m1d2",
                             reparented.find("g1m1d2") != reparented.end());
      auto prt = reparented.at("g1m1d1");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check new g1m1d1 parent", std::string("g1m1b"), prt);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check new g1m1d2 parent", std::string("g1m1b"), prt);
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_get_children);
    CPPUNIT_TEST(test_get_children_with_empty_criteria);
    CPPUNIT_TEST(test_get_unread_children_beginning_thread);
    CPPUNIT_TEST(test_get_unread_children_midddle_thread);
    CPPUNIT_TEST(test_get_unread_children_end_thread);
    CPPUNIT_TEST(test_function_on_exposed_articles);
    CPPUNIT_TEST(test_function_on_reparented_articles);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(DataImplTest::suite());
  bool wasSucessful = runner.run();
  return wasSucessful ? 0 : 1;
}
