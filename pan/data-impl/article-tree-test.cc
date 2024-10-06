#include "pan/data/data.h"
#include "pan/general/string-view.h"
#include <SQLiteCpp/Database.h>
#include <SQLiteCpp/Statement.h>
#include <config.h>
#include <fstream>
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


SQLiteDb get_db()   {
      // always start from an empty db
      char const *db_file("/tmp/data-impl.db");
      std::remove(db_file);
      std::cout << "Creating DB";
      SQLiteDb my_db = SQLiteDb(db_file, SQLite::OPEN_READWRITE|SQLite::OPEN_CREATE);
      return my_db;
}

SQLiteDb pan_db = get_db();

class DataImplTest : public CppUnit::TestFixture
{
private:
    DataImpl *data;

public:
    void setUp()
    {
      StringView cache;
      Prefs prefs;
      data = new DataImpl(cache, prefs);
      load_db_schema(pan_db);
    }

    void tearDown()
    {
    }

    void testSimpleAncestor() {
        pan_db.exec(R"SQL(insert into article (message_id,time_posted) values ("foo", 1234))SQL");
        data->set_reference_tree_in_db(1234, "foo", "baz bar foo");
        SQLite::Statement query(pan_db, R"SQL(
            select one.message_id,other.message_id as parent
            from article as one
            left outer join article as other on other.id == one.parent_id
        )SQL");
        std::string msg_id, parent_id;
        while(query.executeStep()) {
            msg_id = query.getColumn(0).getText();
            parent_id = query.getColumn(1).getText();
            if (msg_id == "foo")
                CPPUNIT_ASSERT_EQUAL(std::string("bar"), parent_id);
            else if (msg_id == "bar")
                CPPUNIT_ASSERT_EQUAL(std::string("baz"), parent_id);
            else if (msg_id == "baz")
                CPPUNIT_ASSERT_EQUAL(std::string(""), parent_id);
            else
                CPPUNIT_FAIL("Unexpected message_id");
        }
    }

    CPPUNIT_TEST_SUITE(DataImplTest);
    CPPUNIT_TEST(testSimpleAncestor);
    CPPUNIT_TEST_SUITE_END();
};

int main()
{

  CppUnit::TextUi::TestRunner runner;
  runner.addTest( DataImplTest::suite() );
  runner.run();
  return 0;
}

/*
Data::ArticleTree *tree;
Data::ArticleTree::articles_t children;
Article const *a;


const Quark server("w00tnewz");
const Quark server2("l33tnews");
const Quark group("alt.religion.buddhism");

data.xover_ref(group);

// TEST: can we add an article?
data.xover_add(server,
               group,
               "Subject",
               "Author",
               0,
               "<article1@foo.com>",
               "",
               40,
               100,
               "");
tree = data.group_get_articles(group, Quark(""));
check(tree->size() == 1ul)
a = tree->get_article("<article1@foo.com>");
check(a != 0);
check(a->subject == "Subject")
check(a->message_id == "<article1@foo.com>")
check(tree->get_parent(a->message_id) == 0)
delete tree;

// TEST: can we add a child?
data.xover_add(server,
               group,
               "Re: Subject",
               "Author",
               0,
               "<article2@blah.com>",
               "<article1@foo.com>",
               40,
               100,
               "");
tree = data.group_get_articles(group, Quark(""));
check(tree->size() == 2ul)
a = tree->get_article("<article2@blah.com>");
check(a != 0)
check(a->subject == "Re: Subject")
check(a->author == "Author")
check(a->message_id == "<article2@blah.com>")
a = tree->get_parent(a->message_id);
check(a != 0)
check(a->message_id == "<article1@foo.com>")
check(a->subject == "Subject")
check(a->author == "Author")
check(tree->get_parent(a->message_id) == 0)
tree->get_children("<article1@foo.com>", children);
check(children.size() == 1u)
check(children[0]->message_id == "<article2@blah.com>")
delete tree;

data.xover_unref(group);

a = 0;
data.group_clear_articles(group);
data.xover_ref(group);

// match articles whose subject has the letter 'a'
TextMatch::Description description;
description.type = TextMatch::CONTAINS;
description.text = "a";
FilterInfo filter_info;
filter_info.set_type_text("Subject", description);

// show articles whose subject has the letter 'a'
tree = data.group_get_articles(group, Quark(""));
tree->set_filter(Data::SHOW_ARTICLES, &filter_info);
check(tree->size() == 0ul);

// TEST: add an article that passes the test and it appears
data.xover_add(server,
               group,
               "a subject",
               "Author",
               0,
               "<article1@foo.com>",
               "",
               40,
               100,
               "");
data.xover_flush(group);
check(tree->size() == 1ul)
a = tree->get_article("<article1@foo.com>");
check(a != 0);
check(a->message_id == "<article1@foo.com>")

// TEST: add an article that doesn't pass the test and doesn't appear
data.xover_add(server,
               group,
               "subject two",
               "Author",
               0,
               "<article2@foo.com>",
               "",
               40,
               100,
               "");
check(tree->size() == 1ul)
check(tree->get_article("<article1@foo.com>") != 0);
check(tree->get_article("<article2@foo.com>") == 0);

data.xover_unref(group);

#if 0
 //Quark q;
 //Article a1, a2, a3;
 quark_v children;
 //check (data.article_get_children (group, a1.message_id, children))
 //check (children.size() == 1u)
 //check (children.front() == a2.message_id)
 //check (data.group_get_article_count(group) == 2ul)

 // add an article that needs a parent that we'll get in a moment with xover...
 a3.subject = "Re: I hope I find my mommy!";
 a3.message_id = "<i-want-my-mommy@foo.com>";
 a3.references = "<i-am-your-mommy@foo.com>";
 data.add_article (group, a3);

 data.xover_ref (group);

 // TEST: does xover add articles and thread them right to existing parents?
 data.xover_add (server, group, "10033	Re: it is all a dream.	\"Bob Barker\"
<bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT
<wftmid@newsread1.news.pas.earthlink.net>	<article2@blah.com>	1000
100	Xref: news.netfront.net alt.zen:175094 alt.religion.buddhism:10033
talk.religion.buddhism:170169");
 //check (data.group_get_article_count(group) == 4ul)
 check (data.group_find_article (group,
"<wftmid@newsread1.news.pas.earthlink.net>", tmp)) check (tmp.subject == "Re: it
is all a dream.") check (tmp.get_line_count() == 100) check
(tmp.get_byte_count() == 1000)
 //check (data.article_get_parent (group, tmp.message_id, q))
 //check (q == a2.message_id)
 check (tmp.parts.size() == 1u)
 part = tmp.get_part (0);
 check (part != 0)
 check (part->xref.find (server, group, ul))
 check (ul == 10033)

 // TEST: does xover add articles and thread them right to existing grandparents
when their parents can't be found?
 data.xover_add (server, group, "10034	Re: it is all a dream again. \"Bob
Barker\" <bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT
<wftmid2@newsread1.news.pas.earthlink.net>	<article1@foo.com>
<article2b@blah.com>	999	90	Xref: news.netfront.net alt.zen:175095
alt.religion.buddhism:10034 talk.religion.buddhism:170168");
 //check (data.group_get_article_count(group) == 5ul)
 check (data.group_find_article (group,
"<wftmid2@newsread1.news.pas.earthlink.net>", tmp)) check (tmp.subject == "Re:
it is all a dream again.") check (tmp.get_line_count() == 90) check
(tmp.get_byte_count() == 999)
 //check (data.article_get_parent (group, tmp.message_id, q))
 //check (q == a1.message_id)
 Article floating_child (tmp);

 // TEST: and when the parent shows up, does the child get reparented right?
 data.xover_add (server, group, "10035	Re: it is all a dream again. \"Bob
Barker\" <bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT <article2b@blah.com>
<article1@foo.com>	2487	31	Xref: news.netfront.net alt.zen:175096
alt.religion.buddhism:10035 talk.religion.buddhism:170169");
 //check (data.group_get_article_count(group) == 6ul)
 check (data.group_find_article (group, "<article2b@blah.com>", tmp))
 check (tmp.subject == "Re: it is all a dream again.")
 //check (data.article_get_parent (group, floating_child.message_id, q))
 //check (q == "<article2b@blah.com>")

 // TEST: when an xover parent of an existing article comes in, does the
existing article get reparented?
 //check (!data.article_get_parent (group, a3.message_id, q))
 data.xover_add (server, group, "10032	I am your mommy.	\"Mommy\"
<mom@mom.com>	Wed, 27 Apr 2005 16:53:02 GMT
<i-am-your-mommy@foo.com>		2487	31	Xref: news.netfront.net
alt.zen:15094 alt.religion.buddhism:1003 talk.religion.buddhism:10169");
 //check (data.group_get_article_count(group) == 7ul)
 //check (data.article_get_parent (group, a3.message_id, q))
 //check (q == "<i-am-your-mommy@foo.com>")

 // TEST: single-part attachment
 data.xover_add (server, group, "10033	Some Attachment (1/1)	\"Bob Barker\"
<bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT <single-complete@foo.com>
<article2@blah.com>	5000	800	Xref: news.netfront.net
alt.zen:175094"); check (data.group_find_article (group,
"<single-complete@foo.com>", tmp)); check (tmp.subject == "Some Attachment
(1/1)") check (tmp.get_line_count() == 800) check (tmp.get_byte_count() == 5000)
 check (tmp.part_state == tmp.COMPLETE);

 // TEST: two-part attachment
 data.xover_add (server, group, "10034	Some Big Attachment (1/2)
\"Bob Barker\" <bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT
<multi-part-1@foo.com>	<article2@blah.com>	5000	800 Xref:
news.netfront.net alt.religion.buddhism:10034 alt.zen:175094"); check
(data.group_find_article (group, "<multi-part-1@foo.com>", tmp)); check
(tmp.subject == "Some Big Attachment (1/2)") check (tmp.get_line_count() == 800)
 check (tmp.get_byte_count() == 5000)
 check (tmp.part_state == tmp.INCOMPLETE);
 part = tmp.get_part (1);
 check (part != 0)
 check (part->xref.find (server, group, ul))
 check (ul == 10034)

 data.xover_add (server, group, "10035	Some Big Attachment (2/2)
\"Bob Barker\" <bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT
<multi-part-2@foo.com>	<article2@blah.com>	999	99
Xref: w00tnewz.com alt.religion.buddhism:10035 alt.zen:10035"); check
(data.group_find_article (group, "<multi-part-1@foo.com>", tmp)); check
(tmp.subject == "Some Big Attachment (1/2)") check (tmp.get_line_count() == 899)
 check (tmp.get_byte_count() == 5999)
 check (tmp.part_state == tmp.COMPLETE);
 part = tmp.get_part (2);
 check (part != 0)
 check (part->xref.find (server, group, ul))
 check (ul == 10035)

 // TEST: multiserver
 data.xover_add (server2, group, "6666	Some Big Attachment (2/2)
\"Bob Barker\" <bob@wtf.com>	Wed, 27 Apr 2005 16:53:02 GMT
<multi-part-2@foo.com>	<article2@blah.com>	5000	800
Xref: l33tnewz.com alt.religion.buddhism:6666 alt.zen:6666");

 ArticleTree * tree = data.group_get_articles (group, FilterInfo());
 const Article * a = tree->get_article ("<multi-part-1@foo.com>");
 check (a != 0);
 check (a->subject == "Some Big Attachment (1/2)")
 check (a->part_state == tmp.COMPLETE)
 part = a->get_part (2);
 check (part != 0);
 check (part->xref.targets.size() == 4)
 check (part->xref.find (server, group, ul))
 check (ul == 10035)
 check (part->xref.find (server2, group, ul))
 check (ul == 6666)

#if 0
 TimeElapsed xover_timer;
 std::ifstream in ("header-impl-test.dat");
 std::string line;
 int lines = 0;
 while (std::getline (in, line)) {
    data.xover_add (server, group, line);
    ++lines;
 }
 Log::get().add_va (Log::INFO, "XOver added %lu headers in %.2f seconds",
    lines,
    xover_timer.get_seconds_elapsed());

 data.unref_group (group);
#endif

 data.xover_unref (group);
 data.unref_group (group);
 data.save ();

 //std::cerr << "headers file: [" << std::endl <<
data_source->_group_headers[group] << std::endl;

#if 0
 ScriptedDataSource * source_2 = new ScriptedDataSource (*data_source);
 DataImpl data2 (source_2);
 check (data2 == data);
#endif
#endif

return 0;
}
*/
