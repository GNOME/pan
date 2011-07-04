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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
extern "C" {
  #include <glib.h>
}
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/general/utf8-utils.h>
#include "nzb.h"
#include "task-article.h"
#include "task-upload.h"

using namespace pan;

namespace
{
  GMimeMessage * import_msg(const StringView filename)
  {
    std::string txt;
    GMimeMessage * msg;
    if (file :: get_text_file_contents (filename, txt))
    {
      GMimeStream * stream = g_mime_stream_mem_new_with_buffer (txt.c_str(), txt.size());
      GMimeParser * parser = g_mime_parser_new_with_stream (stream);
      msg   = g_mime_parser_construct_message (parser);
      g_object_unref (G_OBJECT(parser));
      g_object_unref (G_OBJECT(stream));
    }

    //delete msg
    unlink(filename.str);
    g_object_ref(msg);
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
    std::string rng;
    std::string save_file;
    TaskUpload::EncodeMode enc_mode;
    TaskUpload::needed_t needed_parts;
    PartBatch parts;
    tasks_t tasks;
    Article a;
    ArticleCache& cache;
    ArticleRead& read;
    EncodeCache& encode_cache;
    const ServerRank& ranks;
    const GroupServer& gs;
    Quark server;
    const StringView fallback_path;
    size_t bytes;
    size_t number;
    std::string msg_name;
    int lpf;

    MyContext (ArticleCache& ac, EncodeCache& ec, ArticleRead& r,
               const ServerRank& rank, const GroupServer& g, const StringView& p):
      cache(ac), encode_cache(ec), read(r), ranks(rank), gs(g), fallback_path(p) {}

    void file_clear () {
      groups.clear ();
      text.clear ();
      path.clear ();
      a.clear ();
      bytes = 0;
      number = 0;
      needed_parts.clear();
      save_file.clear();
      rng.clear();
      msg_name.clear();
      enc_mode = TaskUpload::YENC;
      lpf = 5000;
    }
  };

  // called for open tags <foo bar='baz'>
  void start_element (GMarkupParseContext *context         UNUSED,
                      const gchar         *element_name,
                      const gchar        **attribute_names,
                      const gchar        **attribute_vals,
                      gpointer             user_data,
                      GError             **error           UNUSED)
  {
    MyContext& mc (*static_cast<MyContext*>(user_data));

    if (!strcmp (element_name, "file")) {
      mc.file_clear ();
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v) {
             if (!strcmp (*k,"poster"))  mc.a.author = *v;
        else if (!strcmp (*k,"subject")) mc.a.subject = *v;
        else if (!strcmp (*k,"date"))    mc.a.time_posted = strtoul(*v,0,10);
      }
    }

    else if (!strcmp (element_name, "upload")) {
      mc.file_clear ();
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v) {
             if (!strcmp (*k,"server"))     mc.server    = *v;
        else if (!strcmp (*k,"poster"))     mc.a.author = *v;
        else if (!strcmp (*k,"subject"))    mc.a.subject = *v;
        else if (!strcmp (*k,"msg"))        mc.msg_name  = *v;
        else if (!strcmp (*k,"save-file"))  mc.save_file = *v;
        else if (!strcmp (*k,"lpf"))        mc.lpf       = atoi(*v);
        else if (!strcmp (*k,"enc-mode"))   mc.enc_mode  = (TaskUpload::EncodeMode) atoi (*v);

      }
    }

    else if (!strcmp (element_name, "segment") || !strcmp (element_name, "part")) {
      mc.bytes = 0;
      mc.number = 0;
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v) {
             if (!strcmp (*k,"bytes"))  mc.bytes  = strtoul (*v,0,10);
        else if (!strcmp (*k,"number")) mc.number = atoi (*v);
        else if (!strcmp (*k,"rng"))    mc.rng    = *v;
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

    else if (!strcmp(element_name, "segment") && mc.number && !mc.text.empty()) {
      const std::string mid ("<" + mc.text + ">");
      if (mc.a.message_id.empty()) {
        mc.a.message_id = mid;
        mc.parts.init (mid);
      }
      mc.parts.add_part (mc.number, mid, mc.bytes);
    }

    else if (!strcmp(element_name, "part") && !mc.rng.empty() && mc.number && !mc.text.empty()) {
      mc.a.message_id = mc.text;
      TaskUpload::Needed n;
      n.partno = mc.number;
      n.message_id = mc.text;
      n.mid = mc.rng;
      n.bytes = mc.bytes;
      mc.needed_parts.insert(std::pair<int, TaskUpload::Needed>(mc.number,n));
    }

    else if (!strcmp(element_name,"path"))
      mc.path = mc.text;

    else if (!strcmp (element_name, "file"))
    {
      mc.parts.sort ();
      mc.a.set_parts (mc.parts);

      foreach_const (quarks_t, mc.groups, git) {
        quarks_t servers;
        mc.gs.group_get_servers (*git, servers);
        foreach_const (quarks_t, servers, sit)
          mc.a.xref.insert (*sit, *git, 0);
      }
      const StringView p (mc.path.empty() ? mc.fallback_path : StringView(mc.path));
      debug("adding taskarticle from nzb.\n");
      mc.tasks.push_back (new TaskArticle (mc.ranks, mc.gs, mc.a, mc.cache, mc.read, 0, TaskArticle::DECODE, p));

    }
    else if (!strcmp (element_name, "upload"))
    {
      debug("adding taskupload from nzb.\n");
      foreach_const (quarks_t, mc.groups, git)
        mc.a.xref.insert (mc.server, *git, 0);
      TaskUpload::UploadInfo format;
      format.lpf = mc.lpf;
      format.total = std::max(1, (int) (((long)mc.bytes + (mc.lpf*bpl[TaskUpload::YENC]-1)) / (mc.lpf*bpl[TaskUpload::YENC])));

      char buf[2048];
      mc.encode_cache.get_filename (buf, Quark(mc.msg_name));
      GMimeMessage * msg = import_msg(StringView(buf));

      TaskUpload* tmp = new TaskUpload (mc.path, mc.server, mc.encode_cache, mc.a, // subject and author
                                        format, msg, 0, mc.enc_mode);

      /* build needed parts */
      foreach (TaskUpload::needed_t, mc.needed_parts, it)
        tmp->needed().insert(*it);
      tmp->build_needed_tasks();

      mc.tasks.push_back (tmp);
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
  p.passthrough = 0;
  p.error = 0;
  GMarkupParseContext* c (
    g_markup_parse_context_new (&p, (GMarkupParseFlags)0, &mc, 0));
  GError * gerr (0);
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

/* Saves all current tasks to ~/.$PAN_HOME/tasks.nzb */
std::ostream&
NZB :: nzb_to_xml (std::ostream             & out,
                   const std::vector<Task*> & tasks)
{
  int depth (0);

//  // init rng
//  MTRand rng;
//  rng.seed();

  char buf[2048];
  char name[2048];

  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
      << "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.0//EN\" \"http://www.newzbin.com/DTD/nzb/nzb-1.0.dtd\">\n"
      << indent(depth++)
      << "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";

  foreach_const (tasks_t, tasks, it)
  {
    TaskArticle * task (dynamic_cast<TaskArticle*>(*it));
    if (task)
    {

      if (task->get_save_path().empty()) // this task is for reading, not saving...
        continue;

      const Article& a (task->get_article());
      out << indent(depth++)
          << "<file" << " poster=\"";
      escaped (out, a.author.to_view());
      out  << "\" date=\"" << a.time_posted << "\" subject=\"";
      escaped (out, a.subject.to_view()) << "\">\n";

      // path to save this to.
      // This isn't part of the nzb spec.
      const Quark& path (task->get_save_path());
      if (!path.empty()) {
        out << indent(depth) << "<path>";
        escaped (out, path.to_view());
        out << "</path>\n";
      }

      // what groups was this crossposted in?
      quarks_t groups;
      foreach_const (Xref, a.xref, xit)
        groups.insert (xit->group);
      out << indent(depth++) << "<groups>\n";
      foreach_const (quarks_t, groups, git)
        out << indent(depth) << "<group>" << *git << "</group>\n";
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
    }
    else
    { // handle upload tasks
      TaskUpload * task (dynamic_cast<TaskUpload*>(*it));
      // not an upload task, move on
      if (!task) continue;

      const Article& a (task->get_article());

      out << indent(depth)
          << "<upload" << " poster=\"";
      escaped (out, task->_author);
      out << "\" subject=\"";
      escaped (out, task->_subject);
      out << "\" save-file=\"";
      escaped (out, task->_save_file);
      out  << "\" server=\"";
      escaped (out, task->_server.to_string());

      out  << "\" msg=\"";
      /* save gmimemessage from task */
      g_snprintf(name,sizeof(name),"%s_msg_%d.%d",task->_basename.c_str(), task->_queue_pos, task->_bytes);
      task->_cache.get_filename (buf, name);
      std::ofstream outfile (buf, std::ios::out | std::ios::trunc);
      char * body (g_mime_object_to_string ((GMimeObject *) task->_msg));
      outfile << body;
      outfile.close();
      g_free(body);
      escaped (out, name);

      out  << "\" enc-mode=\"";
      g_snprintf(buf,sizeof(buf),"%d",task->_encode_mode);
      escaped (out, buf);
      out  << "\" lpf=\"";
      g_snprintf(buf,sizeof(buf),"%d",task->_lpf);
      escaped (out, buf);
      out <<"\">\n";

      ++depth;
      out << indent(depth)
          << "<path>" << task->_filename << "</path>\n";
      out  << indent(depth) << "<groups>\n";

      ++depth;
      foreach_const (Xref, task->_article.xref,  xit)
        out << indent(depth) << "<group>" << xit->group << "</group>\n";
      --depth;

      out << indent(--depth) << "</groups>\n";
      out  << indent(depth) << "<parts>\n";
      ++depth;

      foreach (TaskUpload::needed_t, task->_needed, it)
      {
        out << indent(depth)
            << "<part" << " bytes=\""<< it->second.bytes << '"'
            << " number=\""          << it->second.partno << '"'
            << " rng=\""             << it->second.mid << '"'
            << ">";
        escaped(out, it->second.message_id);
        out  << "</part>\n";
      }
      --depth;
      out  << indent(depth) << "</parts>\n";
      out << indent(depth) << "</upload>\n";
    }
  }
  out << indent(--depth) << "</nzb>\n";
  return out;
}

/* Saves upload_list to XML file for distribution */
std::ostream&
NZB :: upload_list_to_xml_file (std::ostream   & out,
                   const std::vector<Article*> & tasks)
{
int depth (0);

  foreach_const (std::vector<Article*>, tasks, it)
  {
    Article * task (*it);
    const Article& a (*task);

    out << indent(depth++)
        << "<file" << " poster=\"";
    escaped (out, a.author.to_view());
    out  << "\" date=\"" << a.time_posted << "\" subject=\"";
    escaped (out, a.subject.to_view()) << "\">\n";

    // what groups was this crossposted in?
    quarks_t groups;
    foreach_const (Xref, a.xref, xit)
      groups.insert (xit->group);
    out << indent(depth++) << "<groups>\n";
    foreach_const (quarks_t, groups, git)
      out << indent(depth) << "<group>" << *git << "</group>\n";
    out << indent(--depth) << "</groups>\n";

    // now for the parts...
    out << indent(depth++) << "<segments>\n";
    for (Article::part_iterator it(a.pbegin()), end(a.pend()); it!=end; ++it)
    {
      std::string mid  = it.mid ();

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
  }
  return out;
}

/* Saves selected article-info to a chosen XML file */
std::ostream&
NZB :: nzb_to_xml_file (std::ostream        & out,
                   const std::vector<Task*> & tasks)
{
  int depth (0);

  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n"
      << "<!DOCTYPE nzb PUBLIC \"-//newzBin//DTD NZB 1.0//EN\" \"http://www.newzbin.com/DTD/nzb/nzb-1.0.dtd\">\n"
      << indent(depth++)
      << "<nzb xmlns=\"http://www.newzbin.com/DTD/2003/nzb\">\n";

  foreach_const (tasks_t, tasks, it)
  {
    TaskArticle * task (dynamic_cast<TaskArticle*>(*it));
    if (!task) // not a download task, for example an upload task...
      continue;

    const Article& a (task->get_article());
    out << indent(depth++)
        << "<file" << " poster=\"";
    escaped (out, a.author.to_view());
    out  << "\" date=\"" << a.time_posted << "\" subject=\"";
    escaped (out, a.subject.to_view()) << "\">\n";

    // what groups was this crossposted in?
    quarks_t groups;
    foreach_const (Xref, a.xref, xit)
      groups.insert (xit->group);
    out << indent(depth++) << "<groups>\n";
    foreach_const (quarks_t, groups, git)
      out << indent(depth) << "<group>" << *git << "</group>\n";
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
          << "<segment" << " bytes=\""  << it.bytes() << '"'
                        << " number=\"" << it.number() << '"'
                        << ">";
      escaped(out, mid);
      out  << "</segment>\n";
    }
    out << indent(--depth) << "</segments>\n";
    out << indent(--depth) << "</file>\n";
  }

  out << indent(--depth) << "</nzb>\n";
  return out;
}

