#include <config.h>
#include <iostream>
#include <sstream>
#include <pan/general/string-view.h>
#include <pan/general/test.h>
#include <pan/data/article-cache.h>
#include <pan/data/encode-cache.h>
#include "nzb.h"
#include "task-article.h"

using namespace pan;

struct MyServerRank: public ServerRank
{
  virtual int get_server_rank (const Quark&) const override { return 1; }
};

struct MyGroupServer: public GroupServer
{
  std::map<Quark,quarks_t>& g2s;
  MyGroupServer (std::map<Quark,quarks_t>& g): g2s(g) {}
  virtual ~MyGroupServer () {}
  void server_get_groups (const Quark&, quarks_t&) const override
  { /*ignored*/ }
  void group_get_servers (const Quark& group, quarks_t& setme) const override
  {
    setme.clear ();
    if (g2s.count (group))
      setme = g2s.find(group)->second;
  }
};

struct MyArticleRead: public ArticleRead
{
  virtual ~MyArticleRead () {}
  bool is_read (const Article*) const override { return false; }
  void mark_read (const Article&, bool) override {}
  void mark_read (const Article**, unsigned long, bool) override {}
};


int main ()
{
  const char * test_1 =
     "<?xml version=\"1.0\" encoding=\"iso-8859-1\" ?>\n"
     "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.0//EN\" \"http://www.newzbin.com/DTD/nzb/nzb-1.0.dtd\">\n"
     "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n"
     " <file poster=\"Joe Bloggs <bloggs@nowhere.example>\" date=\"1071674882\" subject=\"Here's your file!  abc-mr2a.r01 (1/2)\">\n"
     "   <groups>\n"
     "     <group>alt.binaries.newzbin</group>\n"
     "     <group>alt.binaries.mojo</group>\n"
     "   </groups>\n"
     "   <segments>\n"
     "     <segment bytes=\"102394\" number=\"1\">123456789abcdef@news.newzbin.com</segment>\n"
     "     <segment bytes=\"4501\" number=\"2\">987654321fedbca@news.newzbin.com</segment>\n"
     "   </segments>\n"
     " </file>\n"
     "</nzb>";

  std::map<Quark,quarks_t> gmap;
  gmap["alt.binaries.newzbin"].insert ("giganews");
  gmap["alt.binaries.newzbin"].insert ("cox");
  gmap["alt.binaries.mojo"].insert ("giganews");
  MyServerRank ranks;
  MyGroupServer gs (gmap);

  ArticleCache cache ("/tmp");
  EncodeCache e_cache ("/tmp");
  MyArticleRead read;
  StringView v (test_1);
  std::vector<Task*> tasks;
  NZB :: tasks_from_nzb_string (v, StringView("/tmp"), cache, e_cache, read, ranks, gs, tasks);
  check (tasks.size() == 1)
  const Article a (dynamic_cast<TaskArticle*>(tasks[0])->get_article());
  check (a.author == "Joe Bloggs <bloggs@nowhere.example>")
  check (a.subject == "Here's your file!  abc-mr2a.r01 (1/2)")
  check (a.get_total_part_count() == 2)
  check (a.get_found_part_count() == 2)
  check (a.message_id == "<123456789abcdef@news.newzbin.com>")
  check (a.time_posted == 1071674882)
  check (a.xref.size() == 3)
  std::string part_mid;
  Parts::bytes_t part_bytes;
  check (a.get_part_info (1u, part_mid, part_bytes))
  check (part_bytes == 102394)
  check (part_mid == "<123456789abcdef@news.newzbin.com>")
  Quark group;
  uint64_t number;
  check (a.xref.find ("cox", group, number))
  check (group == "alt.binaries.newzbin")
  check (number == 0)
  check (a.xref.find ("giganews", group, number))
  check (group=="alt.binaries.newzbin" || group=="alt.binaries.mojo")
  check (number==0)
  check (a.get_part_info (2, part_mid, part_bytes))
  check (part_bytes == 4501)

  std::ostringstream out_stream;
  NZB :: nzb_to_xml (out_stream, tasks);
  std::string out (out_stream.str ());
  std::string expected =
    "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
    "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.0//EN\" \"http://www.newzbin.com/DTD/nzb/nzb-1.0.dtd\">\n"
    "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n"
    "  <file poster=\"Joe Bloggs &lt;bloggs@nowhere.example&gt;\" date=\"1071674882\" subject=\"Here&apos;s your file!  abc-mr2a.r01 (1/2)\">\n"
    "    <path>/tmp</path>\n"
    "    <groups>\n"
    "      <group>alt.binaries.newzbin</group>\n"
    "      <group>alt.binaries.mojo</group>\n"
    "    </groups>\n"
    "    <segments>\n"
    "      <segment bytes=\"102394\" number=\"1\">123456789abcdef@news.newzbin.com</segment>\n"
    "      <segment bytes=\"4501\" number=\"2\">987654321fedbca@news.newzbin.com</segment>\n"
    "    </segments>\n"
    "  </file>\n"
    "</nzb>\n";
  check (out == expected)


  for (std::vector<Task*>::iterator it(tasks.begin()), end(tasks.end()); it!=end; ++it)
    delete *it;

  return 0;
}
