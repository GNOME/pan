/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
#include <sstream>
#include <string>
#include <map>
extern "C" {
  #include <glib.h>
}
#include <pan/general/debug.h>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/general/foreach.h>
#include "nzb.h"
#include "task-article.h"

using namespace pan;

namespace
{
  typedef std::vector<Task*> tasks_t;
  typedef std::map<unsigned int,Article::Part> number_to_part_t;

  struct MyContext
  {
    quarks_t groups;
    std::string text;
    std::string path;
    Article a;
    number_to_part_t parts;
    tasks_t tasks;
    ArticleCache& cache;
    ArticleRead& read;
    const ServerRank& ranks;
    const GroupServer& gs;
    const StringView fallback_path;
    size_t bytes;
    size_t number;

    MyContext (ArticleCache& ac, ArticleRead& r,
               const ServerRank& rank, const GroupServer& g, const StringView& p):
      cache(ac), read(r), ranks(rank), gs(g), fallback_path(p) {}

    void file_clear () {
      groups.clear ();
      text.clear ();
      path.clear ();
      parts.clear ();
      a.clear ();
      bytes = 0;
      number = 0;
    }
  };

  // called for open tags <foo bar='baz'>
  void start_element (GMarkupParseContext *context,
                      const gchar         *element_name,
                      const gchar        **attribute_names,
                      const gchar        **attribute_vals,
                      gpointer             user_data,
                      GError             **error)
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

    else if (!strcmp (element_name, "segment")) {
      mc.bytes = 0;
      mc.number = 0;
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v) {
             if (!strcmp (*k,"bytes"))  mc.bytes = strtoul (*v,0,10);
        else if (!strcmp (*k,"number")) mc.number = atoi (*v);
      }
    }
  }

  // Called for close tags </foo>
  void end_element    (GMarkupParseContext *context,
                       const gchar         *element_name,
                       gpointer             user_data,
                       GError             **error)
  {
    MyContext& mc (*static_cast<MyContext*>(user_data));

    if (!strcmp(element_name, "group"))
      mc.groups.insert (Quark (mc.text));

    else if (!strcmp(element_name, "segment") && mc.number && !mc.text.empty()) {
      const std::string mid ("<" + mc.text + ">");
      if (mc.a.message_id.empty())
          mc.a.message_id = mid;
      Article::Part& part (mc.parts[mc.number]);
      part.bytes = mc.bytes;
      part.set_message_id (mc.a.message_id, mid);
    }

    else if (!strcmp(element_name,"path"))
      mc.path = mc.text;

    else if (!mc.parts.empty() && !strcmp (element_name, "file"))
    {
      // populate mc.a.parts
      mc.a.set_part_count (mc.parts.rbegin()->first);
      foreach (number_to_part_t, mc.parts, it)
        mc.a.get_part(it->first).swap (it->second);

      foreach_const (quarks_t, mc.groups, git) {
        quarks_t servers;
        mc.gs.group_get_servers (*git, servers);
        foreach_const (quarks_t, servers, sit)
          mc.a.xref.insert (*sit, *git, 0);
      }
      const StringView p (mc.path.empty() ? mc.fallback_path : StringView(mc.path));
      mc.tasks.push_back (new TaskArticle (mc.ranks, mc.gs, mc.a, mc.cache, mc.read, 0, TaskArticle::DECODE, p));
    }
  }

  void text (GMarkupParseContext *context,
             const gchar         *text,
             gsize                text_len,  
             gpointer             user_data,
             GError             **error)
  {
    static_cast<MyContext*>(user_data)->text.assign (text, text_len);
  }
}

void
NZB :: tasks_from_nzb_string (const StringView      & nzb,
                              const StringView      & save_path,
                              ArticleCache          & cache,
                              ArticleRead           & read,
                              const ServerRank      & ranks,
                              const GroupServer     & gs,
                              std::vector<Task*>    & appendme)
{
  MyContext mc (cache, read, ranks, gs, save_path);
  GMarkupParser p;
  p.start_element = start_element;
  p.end_element = end_element;
  p.text = text;
  p.passthrough = 0;
  p.error = 0;
  GMarkupParseContext* c (
    g_markup_parse_context_new (&p, (GMarkupParseFlags)0, &mc, 0));
  GError * gerr (0);
  if (g_markup_parse_context_parse (c, nzb.str, nzb.len, &gerr)) {
    // FIXME
  }
  g_markup_parse_context_free (c);
  appendme.insert (appendme.end(), mc.tasks.begin(), mc.tasks.end());
}

void
NZB :: tasks_from_nzb_file (const StringView      & filename,
                            const StringView      & save_path,
                            ArticleCache          & c,
                            ArticleRead           & r,
                            const ServerRank      & ranks,
                            const GroupServer     & gs,
                            std::vector<Task*>    & appendme)
{
  gchar * txt (0);
  gsize len (0);
  if (g_file_get_contents (filename.to_string().c_str(), &txt, &len, 0))
    tasks_from_nzb_string (StringView(txt,len), save_path, c, r, ranks, gs, appendme);
  g_free (txt);
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

std::ostream&
NZB :: nzb_to_xml (std::ostream             & out,
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
    if (!task) // not a download task...
      continue;
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
    int part_number (0);
    foreach_const (Article::parts_t, a.parts, pit)
    {
      ++part_number;

      // incomplete multipart...
      if (pit->empty())
        continue;

      // remove the surrounding < > as per nzb spec
      std::string mid (pit->get_message_id (a.message_id));
      if (mid.size()>=2 && mid[0]=='<') {
        mid.erase (0, 1);
        mid.resize (mid.size()-1);
      }

      // serialize this part
      out << indent(depth)
          << "<segment" << " bytes=\"" << pit->bytes << '"'
                        << " number=\"" << part_number << '"'
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
