#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data/data.h"
#include "pan/general/quark.h"
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
#include <cppunit/extensions/HelperMacros.h>
#include <cppunit/ui/text/TestRunner.h>
#include <vector>

using namespace pan;

char const *db_file("/tmp/my-tree-test.db");
SQLiteDb pan_db(db_file, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);

class DataImplTest : public CppUnit::TestFixture
{
  private:
    DataImpl *data;
    HeaderFilter hf;
    FilterInfo criteria;
    Data::ArticleTree *tree;
    PrefsFile *prefs;

    std::map<std::string, std::string> propMap = {{"hidden", "h"},
                                                  {"exposed", "e"},
                                                  {"reparented", "r"},
                                                  {"shown", "s"}};

  public:
    void setUp()
    {
      StringView cache;
      prefs = new PrefsFile("/tmp/dummy.xml");
      // cleanup whatever may be left from previous test runs
      load_db_schema(pan_db);

      pan_db.exec(R"SQL(
         delete from article_part;
         delete from article_xref;
         delete from article_group;
         delete from ghost;
         delete from article_view;
         delete from article;
         delete from server_group;
         delete from `group`;
         delete from author;
         delete from subject;
         delete from server;
      )SQL");
      data = new DataImpl(cache, *prefs, true);
      pan_db.exec(R"SQL(
        insert into author (author)
               values ("Me <me@home>"),("Other <other@home>");
        insert into subject (subject) values ("blah");
        insert into server (host, port, pan_id, newsrc_filename)
                    values ("dummy", 2, 1, "/dev/null");
        insert into `group` (name, migrated) values ("g1", 1),("g2",1);
      )SQL");
      criteria.clear();
      // That's dumb but that how it's done in header-pane.cc.
      criteria.set_type_aggregate_and();

      // g1m1a -> g1m1b +--> g1m1c1 -> g1m1d1
      //                 \-> g1m1c2 -> g1m1d2
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
        insert into article (message_id,author_id, subject_id, time_posted)
          values (?, (select id from author where author like "Me%"),
                     (select id from subject where subject = "blah"), 1234);
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

    void mark_to_delete_article(std::string msg_id)
    {
      // mark articles as pending deletion
      SQLite::Statement mark_to_delete_q(pan_db, R"SQL(
        update article set to_delete = True where message_id == ?
      )SQL");
      mark_to_delete_q.bind(1, msg_id);
      int res(mark_to_delete_q.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("deleted article " + msg_id, 1, res);
    }

    void assert_children(std::string label,
                       Quark const &parent_mid,
                       Quark const &group,
                       std::vector<std::string> expected_children_mid)
    {
      std::vector<Article> setme;

      tree->get_children_sql(parent_mid, group, setme);

      std::sort(setme.begin(),
                setme.end(),
                [](Article a, Article b)
                {
                  return a.message_id.to_string() < b.message_id.to_string();
                });
      std::sort(expected_children_mid.begin(), expected_children_mid.end());

      std::string user_message(label + " "
                               + (parent_mid.empty() ? "root" : parent_mid.to_string()));

      for (int i(0); i < expected_children_mid.max_size() && i < setme.size(); i++)
      {
        CPPUNIT_ASSERT_EQUAL_MESSAGE(
          user_message + " child msg_id ", expected_children_mid[i], setme[i].message_id.to_string());
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(user_message + " children count",
                                   int(expected_children_mid.size()),
                                   int(setme.size()));
    }

    void assert_hidden(std::string label, std::string msg_id)
    {
      assert_prop(label, "hidden", msg_id);
    }

    void assert_exposed(std::string label, std::string msg_id)
    {
      assert_prop(label, "exposed", msg_id);
    }

    void assert_shown(std::string label, std::string msg_id)
    {
      assert_prop(label, "shown", msg_id);
    }

    void assert_reparented(std::string label, std::string msg_id)
    {
      assert_prop(label, "reparented", msg_id);
    }

    void assert_prop(std::string label, std::string prop, std::string msg_id) {

      SQLite::Statement q(pan_db, R"SQL(
        select status from article_view as av
        join article on article.id == av.article_id
        where message_id = ?
      )SQL");
      q.bind(1, msg_id);

      std::string status;
      while (q.executeStep()) {
        status = q.getColumn(0).getText();
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " msg_id " + msg_id + " property",
                                   propMap.at(prop), status);
    };

    void assert_not_in_view(std::string label,
                     std::string msg_id)
    {
      SQLite::Statement q(pan_db, R"SQL(
        select count() from article_view as av
        join article on article.id == av.article_id
        where message_id = ?
      )SQL");
      q.bind(1, msg_id);

      int count(0);
      while (q.executeStep())
      {
        count += q.getColumn(0).getInt();
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(
          label + " msg_id " + msg_id + " is not in view", true, count == 0);
    };

    void change_read_status(Quark const mid, bool status)
    {
      SQLite::Statement q(pan_db, R"SQL(
        update article set is_read = ? where message_id = ?
      )SQL");
      q.bind(1, status);
      q.bind(2, mid);
      int count = q.exec();
      CPPUNIT_ASSERT_MESSAGE("change read status msg_id " + mid.to_string(),
                             count == 1);
    }

    // inspect content of article_view and message_id with
    // select message_id, article_id, is_read, av.parent_id, status
    // from article join article_view as av on av.article_id = article.id

    // emulates showing all articles, with no criteria
    void test_get_children()
    {
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES);
      tree->initialize_article_view();
      assert_children("", Quark(), "g1", {"g1m1a", "g1m2a", "g1m2"});
      assert_children("", "g1m1a", "g1", {"g1m1b"});
      assert_children("", "g1m1b", "g1", {"g1m1c1", "g1m1c2"});
      assert_children("", "g1m1c1", "g1", {"g1m1d1"});
      assert_children("", "g1m1c2", "g1", {"g1m1d2"});
      assert_exposed("", "g1m1a");

      tree->update_article_after_gui_update();
      assert_shown("step 2", "g1m1a");
    }

    // emulates showing all articles, with no criteria
    void test_get_children_with_empty_criteria()
    {
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      assert_children("", Quark(), "g1", {"g1m1a", "g1m2a", "g1m2"});
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

      std::vector<std::string> original_roots(
        {"g1m1c1", "g1m1c2", "g1m2a", "g1m2"});

      assert_children("step 1", Quark(), "g1", original_roots);

      // the read articles (g1m1a and g1m1b) are not in tree
      assert_not_in_view("step 1", "g1m1a");
      assert_not_in_view("step 1", "g1m1b");
      assert_children("step 1", "g1m1c1", "g1", {"g1m1d1"});
      assert_children("step 1", "g1m1c2", "g1", {"g1m1d2"});
      assert_exposed("step 1", "g1m1c1");

      // now emulate GUI update
      tree->update_article_after_gui_update();
      assert_shown("gui 1", "g1m1c1");

      // read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // new articles in root
      assert_children(
        "step 2", Quark(), "g1", {"g1m1c1", "g1m1d2", "g1m2a", "g1m2"});

      // check that read article is going to be removed
      assert_hidden("step 2", "g1m1c2");

      // check that shown articles have not changed
      assert_shown("gui 2", "g1m1c1");

      // now emulate GUI update
      tree->update_article_after_gui_update();

      // mark same article as unread
      change_read_status("g1m1c2", false);
      tree->update_article_view();
      // check content of added article and reparented ?
      assert_children("step 3", Quark(), "g1", original_roots);
      assert_exposed("step 3", "g1m1c2");
    }

    // emulates showing unread articles with 2 read article in the middle of a
    // thread
    void test_get_unread_children_middle_thread()
    {
      // start test with 2 read articles in the middle of the thread
      change_read_status("g1m1b", true);
      change_read_status("g1m1c1", true);

      // init article view
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      assert_exposed("step 1", "g1m1d1");
      assert_exposed("step 1", "g1m1c2");

      std::vector<std::string> original_roots({"g1m1a", "g1m2a", "g1m2"});
      assert_children("step 1", Quark(), "g1", original_roots);
      // the read articles (g1m1b and g1m1c1) are not in tree
      assert_children("step 1", "g1m1a", "g1", {"g1m1c2", "g1m1d1"});

      tree->update_article_after_gui_update();

      // now read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // no change in root
      assert_children("step 2", Quark(), "g1", original_roots);
      // g1m1c2 is removed from tree, new child is g1m1d2
      assert_children("step 2", "g1m1a", "g1", {"g1m1d1", "g1m1d2"});
      assert_hidden("step 2", "g1m1c2");
      assert_shown("step 2", "g1m1d1");
      assert_reparented("step 2", "g1m1d2");

      tree->update_article_after_gui_update();

      // mark an article as unread
      change_read_status("g1m1c1", false);
      tree->update_article_view();
      // no change in root
      assert_children("step 3", Quark(), "g1", original_roots);
      // g1m1c1 is back in tree
      assert_children("step 3", "g1m1a", "g1", {"g1m1c1", "g1m1d2"});
      assert_exposed("step 3", "g1m1c1");
      // hidden articles should not be flagged as reparented
      assert_not_in_view("step 3", "g1m1c2");
      assert_reparented("step 3", "g1m1d1");
      assert_shown("step 3", "g1m1d2");
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

      std::vector<std::string> original_roots({"g1m1a", "g1m2a", "g1m2"});
      assert_children("step 1", Quark(), "g1", original_roots);

      // the read articles are no longer in tree
      assert_children("step 1", "g1m1a", "g1", {"g1m1b"});
      assert_children("step 1", "g1m1b", "g1", {"g1m1c2"});

      tree->update_article_after_gui_update();

      // now read another article
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      // no change in root
      assert_children("step 2", Quark(), "g1", original_roots);

      // g1m1b has no child
      assert_children("step 2", "g1m1a", "g1", {"g1m1b"});
      assert_hidden("step 2", "g1m1c2");
      assert_not_in_view("step 2", "g1m1d1");
      assert_not_in_view("step 2", "g1m1d2");

      tree->update_article_after_gui_update();

      // mark an article as unread
      change_read_status("g1m1c1", false);
      tree->update_article_view();
      // no change in root
      assert_children("step 3", Quark(), "g1", original_roots);
      assert_children("step 3", "g1m1a", "g1", {"g1m1b"});
      // g1m1c1 is back in tree
      assert_children("step 3", "g1m1b", "g1", {"g1m1c1"});
      assert_not_in_view("step 3", "g1m1c2");
      assert_exposed("step 3", "g1m1c1");
      assert_not_in_view("step 3", "g1m1d1");
      assert_not_in_view("step 3", "g1m1d2");
    }

    // emulates deleting an article
    void test_article_deletion()
    {
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      tree->update_article_after_gui_update();

      // g1m1a -> g1m1b +-->[g1m1c1]-> g1m1d1
      //                 \-> g1m1c2 ->[g1m1d2]
      mark_to_delete_article("g1m1c1");
      mark_to_delete_article("g1m1d2");
      tree->update_article_view();

      assert_hidden("step 1", "g1m1c1");
      assert_hidden("step 1", "g1m1d2");
      assert_reparented("step 1", "g1m1d1");
      assert_shown("step 1", "g1m1c2");

      tree->update_article_after_gui_update();

      tree->update_article_view();
      assert_not_in_view("step 3", "g1m1c1");
      assert_not_in_view("step 3", "g1m1d2");
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_get_children);
    CPPUNIT_TEST(test_get_children_with_empty_criteria);
    CPPUNIT_TEST(test_get_unread_children_beginning_thread);
    CPPUNIT_TEST(test_get_unread_children_middle_thread);
    CPPUNIT_TEST(test_get_unread_children_end_thread);
    CPPUNIT_TEST(test_article_deletion);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(DataImplTest::suite());
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
