#include "pan/data-impl/data-impl.h"
#include "pan/data-impl/header-filter.h"
#include "pan/data/data.h"
#include "pan/general/quark.h"
#include "pan/general/string-view.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <config.h>
#include <cstdint>
#include <fmt/format.h>
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
    }

    void add_test_articles()
    {
      // g1m1a -> g1m1b +--> g1m1c1 -> g1m1d1
      //                 \-> g1m1c2 -> g1m1d2
      add_article_in_group("g1m1a", "g1");
      add_article_in_group("g1m1b", "g1");
      data->store_references("g1m1b", "g1m1a"); // add ancestor
      add_article_in_group("g1m1c1", "g1");
      data->store_references("g1m1c1", "g1m1a g1m1b"); // add ancestors
      add_article_in_group("g1m1c2", "g1");
      data->store_references("g1m1c2", "g1m1a g1m1b"); // add ancestors
      add_article_in_group("g1m1d1", "g1");
      data->store_references("g1m1d1", "g1m1a g1m1b g1m1c1"); // add ancestors
      add_article_in_group("g1m1d2", "g1");
      data->store_references("g1m1d2", "g1m1a g1m1b g1m1c2"); // add ancestors

      // g1m2a -> g1m2b -> g1m2c
      add_article_in_group("g1m2a", "g1");
      add_article_in_group("g1m2b", "g1");
      data->store_references("g1m2b", "g1m2a"); // add ancestor
      add_article_in_group("g1m2c", "g1");
      data->store_references("g1m2c", "g1m2a g1m2b"); // add ancestors

      add_article_in_group("g1m2", "g1"); // no ancestors
    }

    void tearDown() {}

    void add_article_db(std::string msg_id, std::string auth) {
      static int time(1234);
      SQLite::Statement q_article(pan_db, R"SQL(
        insert into article (message_id,author_id, subject_id, time_posted)
          values (?, (select id from author where author like ?),
                     (select id from subject where subject = "blah"), ?);
      )SQL");
      q_article.bind(1, msg_id);
      q_article.bind(2, auth);
      q_article.bind(3, time++);
      int res(q_article.exec());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("insert article " + msg_id, 1, res);
    }

    void add_article_in_group(std::string msg_id, std::string group,
                              std::string auth = "Me%") {
      add_article_db(msg_id, auth);
      link_article_in_group_db(msg_id, group);
    }

    void link_article_in_group_db(std::string msg_id, std::string group)
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

    void assert_parent_mid(std::string label,
                       Quark const &mid,
                       std::string expected_parent_mid)
    {
      std::string str("select av.parent_id from article "
                      " join article_view as av on av.article_id == article.id"
                      " where message_id == ?");

      SQLite::Statement q(pan_db, str);
      q.bind(1, mid);

      while (q.executeStep()) {
        std::string parent_msg_id = q.getColumn(0);
        auto str = fmt::format("{} parent msg_id", label);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(str, expected_parent_mid, parent_msg_id);
      }
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
      std::string str("select message_id from article "
                      " join article_view as av on av.article_id == article.id"
                      " where av.status is not \"h\" and av.parent_id ");
      str += parent_mid.empty() ? "isnull" :
                           "= (select id from article where message_id == ?)";
      str += " order by message_id asc";

      SQLite::Statement q(pan_db, str);

      if (! parent_mid.empty())
      {
        q.bind(1, parent_mid);
      }

      std::sort(expected_children_mid.begin(), expected_children_mid.end());

      std::string user_message(label + " "
                               + (parent_mid.empty() ? "root" : parent_mid.to_string()));
      int count(0);
      while (q.executeStep()) {
        std::string msg_id = q.getColumn(0);
        auto str = fmt::format("{} child msg_id {}", user_message, count);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(str, expected_children_mid[count], msg_id);
        count++;
      }

      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        user_message + " children count", int(expected_children_mid.size()), count);
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

    void assert_equal_threads(std::string label, int line,
                              std::vector<Data::ArticleTree::Thread> expected,
                              std::vector<Data::ArticleTree::Thread> actual) {

      CPPUNIT_ASSERT_EQUAL_MESSAGE(label + " nb of threads", expected.size(), actual.size());

      for (int i = 0; i < expected.size(); i++) {
        auto l = fmt::format("line {} {} thread {}: ", line, label, i);
        assert_equal_thread(l, expected[i], actual[i]);
      }
    }

    void assert_equal_thread(std::string l,
                             Data::ArticleTree::Thread a,
                             Data::ArticleTree::Thread b) {
      CPPUNIT_ASSERT_EQUAL_MESSAGE(l + "parent_id", a.parent_id.to_string(),
                                   b.parent_id.to_string());
      CPPUNIT_ASSERT_EQUAL_MESSAGE(l + "nb of children", a.parent_id.size(),
                                   b.parent_id.size());

      for (int i = 0; i < a.children.size(); i++) {
        auto l2 = fmt::format("{}child {} ", l, i);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(l2 + "msg_id",
                                     a.children[i].msg_id.to_string(),
                                     b.children[i].msg_id.to_string());
        CPPUNIT_ASSERT_EQUAL_MESSAGE(l2 + "sort index",
                                     a.children[i].sort_index,
                                     b.children[i].sort_index);
        char s(a.children[i].status);
        bool ok = s == 'h' || s == 'e' || s == 'r' || s == 's';
        CPPUNIT_ASSERT_MESSAGE(l2 + "status correct",ok);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(l2 + "status match",
                                     a.children[i].status,
                                     b.children[i].status);
      }
    }

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
      add_test_articles();
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES);
      tree->initialize_article_view();

      // assert results in DB
      assert_children("", Quark(), "g1", {"g1m1a", "g1m2a", "g1m2"});
      assert_children("", "g1m1a", "g1", {"g1m1b"});
      assert_children("", "g1m1b", "g1", {"g1m1c1", "g1m1c2"});
      assert_children("", "g1m1c1", "g1", {"g1m1d1"});
      assert_children("", "g1m1c2", "g1", {"g1m1d2"});
      assert_exposed("", "g1m1a");

      std::string p_msg_id(tree->get_parent(Quark("g1m1c1")).message_id);
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check parent", std::string("g1m1b"), p_msg_id);

      tree->update_article_after_gui_update();
      assert_shown("step 2", "g1m1a");
    }

    // emulates showing all articles, with no criteria
    void test_get_children_with_empty_criteria() {
      add_test_articles();
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES,
                                      &criteria);
      tree->initialize_article_view();

      assert_children("", Quark(), "g1", {"g1m1a", "g1m2a", "g1m2"});
    }

    // emulates showing unread articles with 2 read article in the beginning of
    // a thread
    void test_get_unread_children_beginning_thread()
    {
      add_test_articles();
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
      add_test_articles();
      // start test with 2 read articles in the middle of the thread
      change_read_status("g1m1b", true);
      change_read_status("g1m1c1", true);

      // init article view
      criteria.set_type_is_unread();
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES,
                                      &criteria);
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
      add_test_articles();
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
      add_test_articles();
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

    // emulates showing article using a callback, and with articles
    // sorted in hierarchy
    void test_function_on_shown_articles()
    {
      add_test_articles();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      std::vector<Data::ArticleTree::Thread> threads;

      // function that checks that parent_id is either null or already
      // provided.
      std::set<std::string> stack;
      int step(1);

      int count = tree->get_shown_threads(threads, pan::Data::COL_DATE, true);
      CPPUNIT_ASSERT_EQUAL_MESSAGE("step 1 shown count ", 10, count);
      for (Data::ArticleTree::Thread a_thread : threads) {
        Quark prt_id(a_thread.parent_id);

        if (!prt_id.empty()) {
          auto search = stack.find(prt_id.to_string());
          CPPUNIT_ASSERT_MESSAGE("step " + std::to_string(step++) +
                                     " parent_id " + prt_id.to_string() +
                                     " consistency",
                                 search != stack.end());
        }
        for (Data::ArticleTree::Thread::Child child : a_thread.children) {
          stack.insert(child.msg_id.to_string());
        }
      }
    }

    // emulates showing article using a callback, and with articles
    // sorted in hierarchy
    void test_article_sort()
    {
      // add articles with test values
      pan_db.exec(R"SQL(
        insert into subject (subject) values ("Bla"), ("Re: Bla"), ("Meh");
        insert into article (message_id,author_id, subject_id, time_posted, part_state, line_count, bytes)
          values
            ("g1m1", (select id from author where author like "Me%"),
                     (select id from subject where subject = "Bla"), 1234, "I", 10, 56),
            ("g1m1a", (select id from author where author like "Me%"),
                      (select id from subject where subject = "Re: Bla"), 1241, "I", 30, 300),
            ("g1m1b", (select id from author where author like "Me%"),
                      (select id from subject where subject = "Re: Bla"), 1251, "I", 20, 200),
            ("g1o1", (select id from author where author like "Other%"),
                     (select id from subject where subject = "Meh"), 1235, "C", 11, 65);
      )SQL");

      // add articles in group
      SQLite::Statement q(pan_db, "select message_id from article");
      while (q.executeStep()) {
        std::string msg_id = q.getColumn(0);
        link_article_in_group_db(msg_id, "g1");
      }

      // add dummy scores in rows created by add_article_in_group_db
      pan_db.exec(R"SQL(
        update article_group set score = 5
          where article_id == (select id from article where message_id == "g1m1");
        update article_group set score = 6
          where article_id == (select id from article where message_id == "g1o1");
        update article_group set score = 7
          where article_id == (select id from article where message_id == "g1m1a");
        update article_group set score = 8
          where article_id == (select id from article where message_id == "g1m1b");
      )SQL");


      // add ancestor
      data->store_references("g1m1a", "g1m1");
      data->store_references("g1m1b", "g1m1");

      // setup my_tree
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES,
                                      &criteria);
      tree->initialize_article_view();

      // test results
      struct Exp {
        std::string label;
        pan::Data::header_column_enum col;
        bool asc;
        std::vector<std::vector<std::string>> expect;
      };

      std::vector<std::string> in({"g1m1a","g1m1b"});
      Exp all_tests[] = {
          {"state desc", pan::Data::COL_STATE, false, {{"g1m1","g1o1"},in}},
          {"subject desc", pan::Data::COL_SUBJECT, false, {{"g1o1","g1m1"}, in}},
          {"score asc", pan::Data::COL_SCORE, false, {{"g1o1","g1m1"}, in}},
          {"score desc", pan::Data::COL_SCORE, false, {{"g1o1","g1m1"}, in}},
          {"author desc", pan::Data::COL_SHORT_AUTHOR, false, {{"g1o1","g1m1"}, in}},
          {"author asc", pan::Data::COL_SHORT_AUTHOR, true, {{"g1m1","g1o1"},in}},
          {"lines desc", pan::Data::COL_LINES, false, {{"g1o1","g1m1"}, in}},
          {"bytes desc", pan::Data::COL_BYTES, false, {{"g1o1","g1m1"}, in}},
          {"date asc", pan::Data::COL_DATE, true, {{"g1m1","g1o1"},in}},
      };

      for (const auto a_test : all_tests) {
        std::vector<Data::ArticleTree::Thread> threads;
        tree->get_shown_threads(threads, a_test.col, a_test.asc);

        CPPUNIT_ASSERT_EQUAL_MESSAGE(a_test.label + " 1 - check thread count",
                                     a_test.expect.size(), threads.size());

        for (int thread_idx = 0; thread_idx < threads.size(); thread_idx++) {
          std::vector<Data::ArticleTree::Thread::Child> test_children(
              threads[thread_idx].children);
          CPPUNIT_ASSERT_EQUAL_MESSAGE(
              "check article count in thread " + std::to_string(thread_idx),
              a_test.expect[thread_idx].size(), test_children.size());

          for (int64_t child_idx = 0; child_idx < test_children.size();
               child_idx++) {
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                "1. step " + std::to_string(thread_idx) + "." +
                    std::to_string(child_idx) + " sort by " + a_test.label,
                a_test.expect[thread_idx][child_idx],
                test_children[child_idx].msg_id.to_string());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                "1. step " + std::to_string(thread_idx) + "." +
                    std::to_string(child_idx) + " sort index " + a_test.label,
                child_idx+1,
                test_children[child_idx].sort_index);
          }
        }
        threads.clear();

        tree->get_sorted_shown_threads(threads, a_test.col, a_test.asc);
        CPPUNIT_ASSERT_EQUAL_MESSAGE(a_test.label + " 2 - check thread count",
                                     a_test.expect.size(), threads.size());

        for (int thread_idx = 0; thread_idx < threads.size(); thread_idx++) {
          std::vector<Data::ArticleTree::Thread::Child>
              test_children(threads[thread_idx].children);
          CPPUNIT_ASSERT_EQUAL_MESSAGE(
              "check article count in thread " + std::to_string(thread_idx),
              a_test.expect[thread_idx].size(), test_children.size());

          for (int64_t child_idx = 0; child_idx < test_children.size();
               child_idx++) {
            CPPUNIT_ASSERT_EQUAL_MESSAGE("2. step " + std::to_string(thread_idx) +
                                             "." + std::to_string(child_idx) +
                                             " sort by " + a_test.label,
                                         a_test.expect[thread_idx][child_idx],
                                         test_children[child_idx].msg_id.to_string());
            CPPUNIT_ASSERT_EQUAL_MESSAGE(
                "2. step " + std::to_string(thread_idx) + "." +
                    std::to_string(child_idx) + " sort index " + a_test.label,
                child_idx+1,
                test_children[child_idx].sort_index);
          }
        }
      }
    }

    void test_exposed_articles_from_scratch()
    {
      // init article view on empty group
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      // emulate get new headers
      add_test_articles();
      tree->update_article_view();

      // we can view the 3 read articles. They are in separate threads
      // so they have no children
      assert_children("step 1", Quark(), "g1", {"g1m1a", "g1m2a", "g1m2"});
      assert_exposed("step 1", "g1m1a");
    }

    // emulates showing article using a callback, and with articles
    // sorted in hierarchy
    void test_function_on_exposed_articles()
    {
      add_test_articles();
      change_read_status("g1m2", true);
      change_read_status("g1m1a", true);
      change_read_status("g1m1c1", true);
      //[g1m1a]-> g1m1b +-->[g1m1c1]-> g1m1d1
      //                 \-> g1m1c2 -> g1m1d2
      // g1m2a -> g1m2b -> g1m2c
      //[g1m2]

      // step 1 - init article view
      std::string step = "step 1";
      criteria.set_type_is_read();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      std::vector<Data::ArticleTree::Thread> threads, exp_threads;

      // we can view the 3 read articles. They are in separate threads
      // so they have no children
      assert_children(step, Quark(), "g1", {"g1m1a", "g1m2"});

      exp_threads = {
        {"",      {{"g1m1a", 'e', 1}, {"g1m2",'e', 2}}},
        {"g1m1a", {{"g1m1c1",'e',1}}}
      };
      int count = tree->get_exposed_articles(threads, pan::Data::COL_DATE, true );
      CPPUNIT_ASSERT_EQUAL_MESSAGE(step + " exposed count ", 3, count);
      assert_equal_threads(step, __LINE__, exp_threads, threads);

      tree->update_article_after_gui_update();

      // step 2 - change filter
      // hide read articles and expose non-read articles
      step = "step 2";
      criteria.set_type_is_unread();
      threads.clear();
      tree->set_filter(Data::SHOW_ARTICLES, &criteria);
      tree->update_article_view();

      exp_threads = {
          {"",       {{"g1m1b", 'e',1}, {"g1m2a", 'e',2}}},
          {"g1m1b",  {{"g1m1c2",'e',1}, {"g1m1d1",'e',2}}}, // c2 added before d1
          {"g1m2a",  {{"g1m2b", 'e',1}}},
          {"g1m1c2", {{"g1m1d2",'e',1}}},
          {"g1m2b",  {{"g1m2c", 'e',1}}},
      };

      count = tree->get_exposed_articles(threads, pan::Data::COL_DATE, true );
      CPPUNIT_ASSERT_EQUAL_MESSAGE(step + " exposed count ", 7, count);

      // check that parent_id is either null or already provided.
      std::set<std::string> stack;
      for (Data::ArticleTree::Thread thread : threads) {
        // std::cout << "check " << msg_id << " parent " << prt_id << "\n";
        if (!thread.parent_id.empty()) {
          auto search = stack.find(thread.parent_id.to_string());
          CPPUNIT_ASSERT_MESSAGE(step + " parent_id " +
                                     thread.parent_id.to_string() + " consistency",
                                 search != stack.end());
        }
        for (Data::ArticleTree::Thread::Child child : thread.children)
          stack.insert(child.msg_id.to_string());
      };

      assert_equal_threads(step, __LINE__, exp_threads, threads);

      tree->update_article_after_gui_update();

      // step 3 - reset filter - expose the 3 read articles
      step = "step 3";
      criteria.clear();
      threads.clear();
      tree->set_filter(Data::SHOW_ARTICLES, &criteria);
      tree->update_article_view();
      count = tree->get_exposed_articles(threads, pan::Data::COL_DATE, true );

      exp_threads = {
        {"", {{"g1m1a",'e', 1}, {"g1m2",'e', 3}}},
        {"g1m1b", {{"g1m1c1",'e',1}}}
        };

      assert_equal_threads(step, __LINE__, exp_threads, threads);
    }

    // check reparented status of article.
    void test_function_on_reparented_articles()
    {
      add_test_articles();
      // init article view, some articles are shown
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();


      tree->update_article_after_gui_update();

      // change status and re-apply filter, some articles are hidden
      // and their children are reparented.
      change_read_status("g1m1c1", true);
      change_read_status("g1m1c2", true);
      tree->update_article_view();
      assert_hidden("step 2", "g1m1c1");
      assert_hidden("step 2", "g1m1c2");
      assert_reparented("step 2", "g1m1d1");
      assert_reparented("step 2", "g1m1d2");

      // msg_id -> new_parent_id
      std::unordered_map<std::string, std::string> reparented;
      auto insert_in_stack = [&reparented](Quark msg_id, Quark prt_id)
      {
        // std::cout << "check reparented " << msg_id << "\n";
        reparented.insert({msg_id.to_string(), prt_id.to_string()});
      };
      int count = tree->call_on_reparented_articles(insert_in_stack);

      CPPUNIT_ASSERT_EQUAL_MESSAGE("check reparented nb", 2, count);
      CPPUNIT_ASSERT_MESSAGE("check reparented g1m1d1",
                             reparented.find("g1m1d1") != reparented.end());
      CPPUNIT_ASSERT_MESSAGE("check reparented g1m1d2",
                             reparented.find("g1m1d2") != reparented.end());
      auto prt = reparented.at("g1m1d1");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check new g1m1d1 parent", std::string("g1m1b"), prt);
      prt = reparented.at("g1m1d2");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check new g1m1d2 parent", std::string("g1m1b"), prt);

      tree->update_article_after_gui_update();

      // read an article just below root so that child article is reparented to root
      change_read_status("g1m1a", true);
      change_read_status("g1m1b", true);
      tree->update_article_view();
      assert_hidden("step 3", "g1m1a");
      assert_hidden("step 3", "g1m1b");
      assert_reparented("step 3", "g1m1d1");
      assert_reparented("step 3", "g1m1d2");
      reparented.clear();
      count = tree->call_on_reparented_articles(insert_in_stack);

      CPPUNIT_ASSERT_EQUAL_MESSAGE("check reparented nb", 2, count);
      CPPUNIT_ASSERT_MESSAGE("check reparented g1m1d1",
                             reparented.find("g1m1d1") != reparented.end());
      CPPUNIT_ASSERT_MESSAGE("check reparented g1m1d2",
                             reparented.find("g1m1d2") != reparented.end());
      prt = reparented.at("g1m1d1");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check new g1m1d1 parent", std::string(""), prt);
      prt = reparented.at("g1m1d2");
      CPPUNIT_ASSERT_EQUAL_MESSAGE(
        "check new g1m1d2 parent", std::string(""), prt);
    }

    // check reparented status of article.
    void test_reparent_to_root()
    {
      add_test_articles();
      criteria.set_type_is_unread();

      // g1m1a -> g1m1b +--> g1m1c1 -> g1m1d1
      //                 \-> g1m1c2 -> g1m1d2

      // init article view, some articles are shown
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();
      tree->update_article_after_gui_update();

      // filter on g1m1b, which must be reparented to root, i.e. its
      // parent_id in article_view must by null, even if its parent_id
      // is not null in article table.
      TextMatch::Description d;
      d.text = "g1m1b";
      criteria.set_type_text(Quark("Message-Id"), d);
      tree->set_filter(Data::SHOW_ARTICLES, &criteria);

      tree->update_article_view();
      assert_reparented("step 1", "g1m1b");
      assert_parent_mid("", "g1m1b", "");
    }

    // check hidden status of articles
    void test_function_on_hidden_articles() {
      add_test_articles();
      // init article view, some articles are shown
      criteria.set_type_is_unread();
      tree = data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES,
                                      &criteria);
      tree->initialize_article_view();


      tree->update_article_after_gui_update();
      // check idempotency
      tree->update_article_after_gui_update();

      // change status and re-apply filter, some articles are hidden
      change_read_status("g1m1c1", true);
      change_read_status("g1m1c2", true);

      // emulate article removal
      mark_to_delete_article("g1m1d2");

      tree->update_article_view();
      // check idempotency
      tree->update_article_view();

      assert_hidden("step 2", "g1m1c1");
      assert_hidden("step 2", "g1m1c2");
      assert_reparented("step 2", "g1m1d1");
      assert_shown("step 2", "g1m1b");

      // msg_id -> new_parent_id
      quarks_t hidden;
      int count = tree->get_hidden_articles(hidden);

      CPPUNIT_ASSERT_EQUAL_MESSAGE("check hidden nb", 3, count);
      CPPUNIT_ASSERT_MESSAGE("check hidden g1m1c1",
                             hidden.find("g1m1c1") != hidden.end());
      CPPUNIT_ASSERT_MESSAGE("check hidden g1m1c2",
                             hidden.find("g1m1c2") != hidden.end());
      // hidden via article with pending deletion
      CPPUNIT_ASSERT_MESSAGE("check hidden g1m1d2",
                             hidden.find("g1m1d2") != hidden.end());
    }

    // check hidden status of articles
    void test_get_shown_parent_ids()
    {
      add_test_articles();
      // init article view, some articles are shown
      criteria.set_type_is_unread();
      tree =
        data->group_get_articles("g1", "/tmp", Data::SHOW_ARTICLES, &criteria);
      tree->initialize_article_view();

      // read 2 articles, which are removed from view, then their
      // parents are childless and no longer counted as parents
      change_read_status("g1m1d1", true);
      change_read_status("g1m1d2", true);
      tree->update_article_view();

      std::vector<Quark> result;
      tree->get_shown_parent_ids(result);

      int res_size(result.size());
      CPPUNIT_ASSERT_EQUAL_MESSAGE("check nb of parents", 4, res_size);
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(test_get_children);
    CPPUNIT_TEST(test_get_children_with_empty_criteria);
    CPPUNIT_TEST(test_get_unread_children_beginning_thread);
    CPPUNIT_TEST(test_get_unread_children_middle_thread);
    CPPUNIT_TEST(test_get_unread_children_end_thread);
    CPPUNIT_TEST(test_article_deletion);
    CPPUNIT_TEST(test_function_on_shown_articles);
    CPPUNIT_TEST(test_article_sort);
    CPPUNIT_TEST(test_function_on_exposed_articles);
    CPPUNIT_TEST(test_exposed_articles_from_scratch);
    CPPUNIT_TEST(test_function_on_reparented_articles);
    CPPUNIT_TEST(test_function_on_hidden_articles);
    CPPUNIT_TEST(test_get_shown_parent_ids);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{
  CppUnit::TextUi::TestRunner runner;
  runner.addTest(DataImplTest::suite());
  bool wasSuccessful = runner.run("", false);
  return wasSuccessful ? 0 : 1 ; // compute exit return value
}
