/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset:
2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "nzb.h"
#include "pan/data/article.h"
#include "pan/data/in-memory-article.h"
#include "pan/data/in-memory-parts.h"
#include "task-article.h"

#include <config.h>
#include <cstdint>
#include <iostream>
#include <log4cxx/logger.h>
#include <string>
#include <regex>

#include <glib.h>
#include "pan/general/log4cxx.h"
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/general/utf8-utils.h>
#include <pan/data-impl/article-rules.h>

namespace  {
log4cxx::LoggerPtr logger(getLogger("nzb"));
}

namespace pan {

namespace
{
  GMimeMessage * import_msg(const StringView filename)
  {
    std::string txt;
    GMimeMessage * msg(NULL);
    if (file :: get_text_file_contents (filename, txt))
    {
      GMimeStream * stream = g_mime_stream_mem_new_with_buffer (txt.c_str(), txt.size());
      GMimeParser * parser = g_mime_parser_new_with_stream (stream);
      msg   = g_mime_parser_construct_message (parser, NULL);
      g_object_unref (G_OBJECT(parser));
      g_object_unref (G_OBJECT(stream));
    }

    //delete msg on disk
    unlink(filename.str);
    return msg;
  }
}

namespace
{
  typedef std::vector<Task*> tasks_t;

  struct MyContext
  {
    quarks_t groups;
    std::string text;
    std::string path;
    std::string paused;
    std::string author;
    std::string subject;
    std::string article_message_id;

    int64_t time_posted;
    tasks_t tasks;
    Article db_a;
    ArticleCache& cache;
    EncodeCache& encode_cache;
    ArticleRead& read;
    const ServerRank& ranks;
    const GroupServer& gs;
    Quark server;
    const StringView fallback_path;
    size_t bytes;
    size_t number;

    MyContext (ArticleCache& ac, EncodeCache& ec, ArticleRead& r,
               const ServerRank& rank, const GroupServer& g, const StringView& p):
      cache(ac),
      encode_cache(ec),
      read(r),
      ranks(rank),
      gs(g),
      fallback_path(p),
      bytes(0),
      number(0)
    { }

    void file_clear () {
      groups.clear ();
      text.clear ();
      path.clear ();
      paused.clear ();
      author.clear();
      subject.clear();
      bytes = 0;
      number = 0;
      time_posted = 0;
      subject.clear();
      author.clear();
      db_a.clear();
      article_message_id.clear();
    }
  };

  // create minimal article in DB
  void create_article(MyContext mc)
  {
    StringView auth(mc.author),name,address;
    auth.pop_token(name, '<');
    name.trim();
    auth.pop_token(address, '>');

    SQLite::Statement set_author_q(
      pan_db,
      "insert into `author` (name,address) values (?,?) on conflict do nothing;");
    set_author_q.bind(1, name);
    set_author_q.bind(2, address);
    set_author_q.exec();

    SQLite::Statement set_article_q(pan_db, R"SQL(
      insert into `article` (message_id,subject,author_id,time_posted, binary)
      values ($msg_id, $subject, (select id from author where address = $addr),
              $time_posted, true)
      on conflict (message_id) do nothing
    )SQL");
    set_article_q.bindNoCopy(1, mc.article_message_id);
    set_article_q.bindNoCopy(2, mc.subject);
    set_article_q.bind(3, address);
    set_article_q.bind(4, mc.time_posted);
    LOG4CXX_TRACE(logger, "create nzb article " << mc.article_message_id << " (" << mc.subject <<")");
    set_article_q.exec();
  }

  void change_article_id(std::string old_id, std::string new_id)
  {
    SQLite::Statement check_article_mid_q(pan_db, R"SQL(
      select count() from `article` where message_id = ?
    )SQL");
    check_article_mid_q.bindNoCopy(1, new_id);
    bool found(false);
    while (check_article_mid_q.executeStep()) {
        found = check_article_mid_q.getColumn(0).getInt() == 1;
    }

    if (found) {
        // article is already in DB. we assume we have all parts
        LOG4CXX_TRACE(logger,
                      "deleting duplicate article with message id " << old_id );
        SQLite::Statement delete_article_q(pan_db, R"SQL(
          delete from `article` where message_id = ?
        )SQL");
        delete_article_q.bindNoCopy(1, old_id);
        delete_article_q.exec();
    } else {
      SQLite::Statement set_article_mid_q(pan_db, R"SQL(
        update `article` set message_id = ? where message_id = ?
      )SQL");
      set_article_mid_q.bindNoCopy(1, new_id);
      set_article_mid_q.bindNoCopy(2, old_id);
      LOG4CXX_TRACE(logger,
                    "update article message id from " << old_id << " to "
                                                      << new_id);
      set_article_mid_q.exec();
    }
  }

  bool is_article_part_known(std::string part_mid)
  {
    SQLite::Statement set_part_q(pan_db, R"SQL(
      select count() from `article_part` where part_message_id == ?
    )SQL");
    bool found(false);
    while (set_part_q.executeStep()) {
        found = set_part_q.getColumn(0).getInt() == 1;
    }
    return found;
  }

  void add_article_part(std::string part_mid, MyContext mc)
  {
    SQLite::Statement set_part_q(pan_db, R"SQL(
      insert into `article_part` (article_id, part_number, part_message_id, size)
      values ((select id from article where message_id = ?), ?, ?, ?) on conflict do nothing;
    )SQL");
    set_part_q.bindNoCopy(1, mc.article_message_id);
    set_part_q.bind(2, static_cast<int64_t>(mc.number));
    set_part_q.bindNoCopy(3, part_mid);
    set_part_q.bind(4, static_cast<int64_t>(mc.bytes));
    LOG4CXX_TRACE(logger,
                  "add nzb article part " << mc.number << " " << part_mid
                                          << " to " << mc.article_message_id);
    set_part_q.exec();
  }

  void set_article_xref(std::string mid,
                        Quark s_pan_id,
                        Quark group,
                        int64_t number)
  {
    SQLite::Statement set_article_group_q(pan_db, R"SQL(
      insert into `article_group` (article_id, group_id)
      values (
        (select id from article where message_id = ?),
        (select id from `group` where name = ?)
      ) on conflict (article_id, group_id) do nothing;
    )SQL");

    set_article_group_q.bindNoCopy(1, mid);
    set_article_group_q.bindNoCopy(2, group);
    set_article_group_q.exec();

    SQLite::Statement set_xref_q(pan_db,R"SQL(
      insert into `article_xref` (article_group_id, server_id, number)
      values (
        (select ag.id from article_group as ag
         join `group` as g on g.id == ag.group_id
         join article as a on a.id == ag.article_id
         where a.message_id = ? and g.name = ?),
        (select id from server where pan_id = ?),
        ?
      ) on conflict (article_group_id, server_id) do nothing;
    )SQL");

    set_xref_q.bindNoCopy(1, mid);
    set_xref_q.bindNoCopy(2, group);
    set_xref_q.bindNoCopy(3, s_pan_id);
    set_xref_q.bind(4, number);
    LOG4CXX_TRACE(logger, "add nzb article xref " << group << " to " << mid);
    set_xref_q.exec();
  }

  // called for open tags <foo bar='baz'>
  void start_element (GMarkupParseContext *context         UNUSED,
                      const gchar         *element_name,
                      const gchar        **attribute_names,
                      const gchar        **attribute_vals,
                      gpointer             user_data,
                      GError             **error           UNUSED)
  {
    static Quark null_mid;

    MyContext& mc (*static_cast<MyContext*>(user_data));

    if (!strcmp (element_name, "file")) {
      mc.file_clear ();
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v) {
        if (!strcmp (*k,"poster"))       mc.author = *v;
        else if (!strcmp (*k,"subject")) mc.subject = *v;
        else if (!strcmp (*k,"date"))    mc.time_posted = strtoul(*v,nullptr,10);
      }
    }
    else if (!strcmp (element_name, "segments")) {
        ;
    }
    else if (!strcmp (element_name, "segment")) {
      mc.bytes = 0;
      mc.number = 0;
      for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
      {
        if (! strcmp(*k, "bytes"))
        {
          mc.bytes = strtoul(*v, nullptr, 10);
        }
        else if (! strcmp(*k, "number"))
        {
          mc.number = atoi(*v);
        }
      }
    }
  }

  // Called for close tags </foo>
  void end_element    (GMarkupParseContext *context       UNUSED,
                       const gchar         *element_name,
                       gpointer             user_data,
                       GError             **error         UNUSED)
  {
    MyContext& mc (*static_cast<MyContext*>(user_data));

    if (!strcmp(element_name, "group"))
    {
      mc.groups.insert (Quark (mc.text));
    }

    else if (! strcmp(element_name, "segment") && mc.number
             && ! mc.text.empty())
    {
      std::string const mid("<" + mc.text + ">");
      bool known_part = is_article_part_known(mid);

      if (known_part && mc.number == 1) {
        mc.article_message_id = mid;
      }
      else if (known_part) {
          ;
      }
      else if (mc.article_message_id.empty() && mc.number == 1)
      {
        // first part in nzb file is first part in serie
        mc.article_message_id = mid;
        create_article(mc);
        add_article_part(mid, mc);
      }
      else if (mc.article_message_id.empty())
      {
        // the real message_id is in one of the next segments. Article
        // must be created due to DB constraints and setting dummy
        // message_id is required by DB not null constraint
        mc.article_message_id = "dummy";
        // create article in DB
        create_article(mc);
        add_article_part(mid, mc);
      }
      else if (mc.number == 1)
      {
        if (mc.article_message_id == "dummy")
        {
          // insert real article message id
          change_article_id("dummy", mid);
        }
        mc.article_message_id = mid;
        add_article_part(mid, mc);
      }
      else {
        add_article_part(mid, mc);
      }
    }

    else if (!strcmp(element_name,"path"))
      mc.path = mc.text;

    else if (!strcmp(element_name,"paused"))
      mc.paused = mc.text;

    else if (!strcmp (element_name, "file"))
    {
      foreach_const (quarks_t, mc.groups, git) {
        quarks_t servers;
        mc.gs.group_get_servers (*git, servers);
        foreach_const (quarks_t, servers, sit)
          set_article_xref(mc.article_message_id, *sit, *git, 0);
      }
      const StringView p (mc.path.empty() ? mc.fallback_path : StringView(mc.path));
      /// TODO get action mark read from prefs (?)
      Article dba(mc.article_message_id);

      // TODO: test nzb download with ./debug-build/pan/gui/pan  --nzb ../x-men.nzb -o /tmp
      TaskArticle* a = new TaskArticle (mc.ranks, mc.gs, dba, mc.cache, mc.read, TaskArticle::NO_ACTION, nullptr, TaskArticle::DECODE, p);
      if (mc.paused == "1")
        a->set_start_paused(true);
      mc.tasks.push_back (a);
    }
  }

  void text (GMarkupParseContext *context    UNUSED,
             const gchar         *text,
             gsize                text_len,
             gpointer             user_data,
             GError             **error      UNUSED)
  {
    static_cast<MyContext*>(user_data)->text.assign (text, text_len);
  }
}

void
NZB :: tasks_from_nzb_string (const StringView      & nzb_in,
                              const StringView      & save_path,
                              ArticleCache          & cache,
                              EncodeCache           & encode_cache,
                              ArticleRead           & read,
                              const ServerRank      & ranks,
                              const GroupServer     & gs,
                              std::vector<Task*>    & appendme)
{
  const std::string nzb (clean_utf8 (nzb_in));
  MyContext mc (cache, encode_cache, read, ranks, gs, save_path);
  GMarkupParser p;
  p.start_element = start_element;
  p.end_element = end_element;
  p.text = text;
  p.passthrough = nullptr;
  p.error = nullptr;
  GMarkupParseContext* c (
    g_markup_parse_context_new (&p, (GMarkupParseFlags)0, &mc, nullptr));
  GError * gerr (nullptr);
  g_markup_parse_context_parse (c, nzb.c_str(), nzb.size(), &gerr);
  if (gerr) {
    Log::add_urgent (gerr->message);
    g_error_free (gerr);
  }
  g_markup_parse_context_free (c);
  appendme.insert (appendme.end(), mc.tasks.begin(), mc.tasks.end());
}

void
NZB :: tasks_from_nzb_file (const StringView      & filename,
                            const StringView      & save_path,
                            ArticleCache          & c,
                            EncodeCache           & ec,
                            ArticleRead           & r,
                            const ServerRank      & ranks,
                            const GroupServer     & gs,
                            std::vector<Task*>    & appendme)
{
  std::string nzb;
  if (file :: get_text_file_contents (filename, nzb))
    tasks_from_nzb_string (nzb, save_path, c, ec, r, ranks, gs, appendme);
}

namespace
{
  const int indent_char_len (2);

  std::string indent (int depth) { return std::string(depth*indent_char_len, ' '); }

  std::ostream& escaped (std::ostream& out, const StringView& s_in)
  {
    foreach_const (StringView, s_in, pch)
    {
      switch (*pch) {
        case '&': out << "&amp;"; break;
        case '<': out << "&lt;"; break;
        case '>': out << "&gt;"; break;
        case '\'': out << "&apos;"; break;
        case '"': out << "&quot;"; break;
        default: out << *pch; break;
      }
    }
    return out;
  }
}

namespace {

std::ostream &print_article(
  std::ostream &out,
  Article const &a,
  bool task_dump = false,
  bool paused = false,
  Quark const *path = nullptr)
{
  int depth = 1;
  out << indent(depth++) << "<file" << " poster=\"";
  escaped (out, a.get_author().to_view());
  out  << "\" date=\"" << a.get_time_posted() << "\" subject=\"";
  //This is nasty. pan munges the article title of a multipart article to
  //xxxxxxxxx (/<parts>), but nzb wants (1/<parts>)
  std::string subject(a.get_subject());
  LOG4CXX_DEBUG(logger, "print article (as task " << task_dump << " )subject " << subject);
  //Not doing this for task dump as I'm not entirely sure what task dump expects
  //to load
  if (not task_dump)
  {
      subject = std::regex_replace(subject,
                                   std::regex("\\((/[0-9]*\\))$"),
                                   "(1$1");
  }
  escaped (out, subject) << "\">\n";

  if (task_dump)
  {
    // path to save this to.
    // This isn't part of the nzb spec.
    if (!path->empty()) {
      out << indent(depth) << "<path>";
      escaped (out, path->to_view());
      out << "</path>\n";
    }

    out << indent(depth) <<"<paused>";
    out << paused << "</paused>\n";
  }

  // what groups was this crossposted in?
  SQLite::Statement q(pan_db, R"SQL(
    select g.name as xref from `group` as g
    join article_group as ag on ag.group_id == g.id
    join article as a on a.id == ag.article_id
    where a.message_id == ?
    order by g.name asc
  )SQL");

  q.bindNoCopy(1,a.message_id);
  std::string result;
  out << indent(depth++) << "<groups>\n";
  while (q.executeStep()) {
    out << indent(depth) << "<group>" << q.getColumn(0) << "</group>\n";
  }
  out << indent(--depth) << "</groups>\n";

  // now for the parts...
  out << indent(depth++) << "<segments>\n";
  for (Article::part_iterator it(a.pbegin()), end(a.pend()); it!=end; ++it)
  {
    std::string mid = it.mid ();

    // remove the surrounding < > as per nzb spec
    if (mid.size()>=2 && mid[0]=='<') {
      mid.erase (0, 1);
      mid.resize (mid.size()-1);
    }

    // serialize this part
    out << indent(depth)
        << "<segment" << " bytes=\"" << it.bytes() << '"'
        << " number=\"" << it.number() << '"'
        << ">";
    escaped(out, mid);
    out  << "</segment>\n";
  }
  out << indent(--depth) << "</segments>\n";
  out << indent(--depth) << "</file>\n";
  return out;
}

std::ostream &print_article(
  std::ostream &out,
  InMemoryArticle const &a,
  bool task_dump = false,
  bool paused = false,
  Quark const *path = nullptr)
{
  int depth = 1;
  out << indent(depth++) << "<file" << " poster=\"";
  escaped (out, a.author.to_view());
  out  << "\" date=\"" << a.time_posted << "\" subject=\"";
  //This is nasty. pan munges the article title of a multipart article to
  //xxxxxxxxx (/<parts>), but nzb wants (1/<parts>)
  std::string subject(a.subject);
  //Not doing this for task dump as I'm not entirely sure what task dump expects
  //to load
  if (not task_dump)
  {
      subject = std::regex_replace(subject,
                                   std::regex("\\((/[0-9]*\\))$"),
                                   "(1$1");
  }
  escaped (out, subject) << "\">\n";

  if (task_dump)
  {
    // path to save this to.
    // This isn't part of the nzb spec.
    if (!path->empty()) {
      out << indent(depth) << "<path>";
      escaped (out, path->to_view());
      out << "</path>\n";
    }

    out << indent(depth) <<"<paused>";
    out << paused << "</paused>\n";
  }

  // what groups was this crossposted in?
  quarks_t groups;
  foreach_const (InMemoryXref, a.xref, xit)
    groups.insert (xit->group);
  out << indent(depth++) << "<groups>\n";
  foreach_const (quarks_t, groups, git)
    out << indent(depth) << "<group>" << *git << "</group>\n";
  out << indent(--depth) << "</groups>\n";

  // now for the parts...
  out << indent(depth++) << "<segments>\n";
  for (InMemoryArticle::part_iterator it(a.pbegin()), end(a.pend()); it!=end; ++it)
  {
    std::string mid = it.mid ();

    // remove the surrounding < > as per nzb spec
    if (mid.size()>=2 && mid[0]=='<') {
      mid.erase (0, 1);
      mid.resize (mid.size()-1);
    }

    // serialize this part
    out << indent(depth)
        << "<segment" << " bytes=\"" << it.bytes() << '"'
        << " number=\"" << it.number() << '"'
        << ">";
    escaped(out, mid);
    out  << "</segment>\n";
  }
  out << indent(--depth) << "</segments>\n";
  out << indent(--depth) << "</file>\n";
  return out;
}
}

std::ostream &NZB::print_header(std::ostream &out)
{
  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
      << "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.0//EN\" "
      << "\"http://www.newzbin.com/DTD/nzb/nzb-1.0.dtd\">\n"
      << "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";
  return out;
}

std::ostream &NZB::print_footer(std::ostream &out)
{
  return out << "</nzb>\n";
}

/* Saves all current tasks to ~/.$PAN_HOME/tasks.nzb */
std::ostream&
NZB :: nzb_to_xml (std::ostream             & out,
                   const std::vector<Task*> & tasks)
{
  print_header(out);

  foreach_const (tasks_t, tasks, it)
  {
    TaskArticle * task (dynamic_cast<TaskArticle*>(*it));
    if (task)
    {

      if (task->get_save_path().empty())
      {
        // this task is for reading, not saving...
        continue;
      }

      print_article(out,
                    task->get_article(),
                    true,
                    task->start_paused(),
                    &task->get_save_path());
    }
  }
  return print_footer(out);
}

/* Saves upload_list to XML file for distribution */
std::ostream&
NZB :: upload_list_to_xml_file (std::ostream   & out,
                   const std::vector<InMemoryArticle*> & tasks)
{

  foreach_const (std::vector<InMemoryArticle*>, tasks, it)
  {
    print_article(out, **it);
  }

  return out;
}

/* Saves selected article-info to a chosen XML file */
std::ostream&
NZB :: nzb_to_xml_file (std::ostream        & out,
                   const std::vector<Task*> & tasks)
{
  print_header(out);

  foreach_const (tasks_t, tasks, it)
  {
    TaskArticle * task (dynamic_cast<TaskArticle*>(*it));

    if (!task)
    {
      // not a download task, for example an upload task...
      continue;
    }

    print_article(out, task->get_article());
  }

  return print_footer(out);
}

}
