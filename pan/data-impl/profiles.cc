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
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>
extern "C" {
  #include <sys/types.h> // for chmod
  #include <sys/stat.h> // for chmod
  #include <glib.h>
  #include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/string-view.h>
#include <pan/general/file-util.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include "data-io.h"
#include "profiles.h"

using namespace pan;

///
///  XML Parsing
///
namespace
{
  typedef std::map<std::string,Profile> profiles_t;

  struct MyContext
  {
    bool is_active;
    std::string profile_name;
    std::string text;
    std::string header_name;
    profiles_t& profiles;
    std::string& active_profile;

    MyContext(profiles_t& p, std::string& ap):
      is_active (false),
      profiles (p),
      active_profile (ap) {}
  };

  // called for open tags <foo bar='baz'>
  void start_element (GMarkupParseContext * context,
                      const gchar         * element_name_str,
                      const gchar        ** attribute_names,
                      const gchar        ** attribute_vals,
                      gpointer              user_data,
                      GError             ** error)
  {
    MyContext& mc (*static_cast<MyContext*>(user_data));
    const std::string element_name (element_name_str);

    if (element_name=="profile") {
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
        if (!strcmp(*k,"name"))
          mc.profile_name = *v;
      if (!mc.profile_name.empty())
        mc.profiles[mc.profile_name].clear ();
    } 

    if ((element_name == "signature_file") && !mc.profile_name.empty()) {
      Profile& p (mc.profiles[mc.profile_name]);
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v) {
        if (!strcmp(*k,"active"))
          p.use_sigfile = true;
        else if (!strcmp(*k,"type")) {
          if (!strcmp (*v, "file")) p.sig_type = p.FILE;
          else if (!strcmp (*v, "command")) p.sig_type = p.COMMAND;
          else p.sig_type = p.TEXT;
        }
      }
    }
  }

  // Called for close tags </foo>
  void end_element    (GMarkupParseContext *context,
                       const gchar         *element_name_str,
                       gpointer             user_data,
                       GError             **error)
  {
    MyContext& mc (*static_cast<MyContext*>(user_data));
    const std::string element_name (element_name_str);
    StringView t (mc.text);
    t.trim ();

    if (!mc.profile_name.empty()) {
      Profile& p (mc.profiles[mc.profile_name]);
      if (element_name == "signature_file") p.signature_file.assign (t.str, t.len);
      else if (element_name == "attribution") p.attribution.assign (t.str, t.len);
      else if (element_name == "fqdn") p.fqdn.assign (t.str, t.len);
      else if (element_name == "username") p.username.assign (t.str, t.len);
      else if (element_name == "address") p.address.assign (t.str, t.len);
      else if (element_name == "server") p.posting_server = t;
      else if (element_name == "profile" && mc.is_active) mc.active_profile.assign (t.str, t.len);
      else if (element_name == "name") mc.header_name.assign (t.str, t.len);
      else if ((element_name == "value") && !mc.header_name.empty()) p.headers[mc.header_name].assign (t.str, t.len);
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
ProfilesImpl :: clear ()
{
  profiles.clear ();
  active_profile.clear ();
}

void
ProfilesImpl :: load (const StringView& filename)
{
  std::string txt;
  if (file :: get_text_file_contents (filename, txt))
  {
    MyContext mc (profiles, active_profile);
    GMarkupParser p;
    p.start_element = start_element;
    p.end_element = end_element;
    p.text = text;
    p.passthrough = 0;
    p.error = 0;
    GError * gerr (0);
    GMarkupParseContext* c (g_markup_parse_context_new (&p, (GMarkupParseFlags)0, &mc, 0));
    g_markup_parse_context_parse (c, txt.c_str(), txt.size(), &gerr);
    if (gerr) {
      Log::add_err_va (_("Error reading file \"%s\": %s"), filename.to_string().c_str(), gerr->message);
      g_clear_error (&gerr);
    }
    g_markup_parse_context_free (c);
  }
}

namespace
{
  const int indent_char_len (2);

  std::string indent (int depth) { return std::string(depth*indent_char_len, ' '); }

  std::string escaped (const std::string& s)
  {
    char * pch = g_markup_escape_text (s.c_str(), s.size());
    const std::string ret (pch);
    g_free (pch);
    return ret;
  }
}

void
ProfilesImpl :: serialize (std::ostream& out) const
{
  int depth (0);
 
  // xml header... 
  out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n";
  out << indent(depth++) << "<posting>\n";

  // profiles...
  out << indent(depth++) << "<profiles>\n";
  foreach_const (profiles_t, profiles, it) {
    out << indent(depth++) << "<profile name=\"" << escaped(it->first) << "\">\n";
    out << indent(depth) << "<username>" << escaped(it->second.username) << "</username>\n";
    out << indent(depth) << "<address>" << escaped(it->second.address) << "</address>\n";
    out << indent(depth) << "<server>" << escaped(it->second.posting_server.to_view()) << "</server>\n";
    if (!it->second.signature_file.empty()) {
      const char * type;
      switch (it->second.sig_type) {
        case Profile::FILE: type = "file"; break;
        case Profile::COMMAND: type = "command"; break;
        default: type = "text"; break;
      }
      out << indent(depth) << "<signature_file"
                           << " active=\"" << (it->second.use_sigfile ? "true" : "false") << '"'
                           << " type=\"" << type << '"'
                           << ">" << escaped(it->second.signature_file) << "</signature_file>\n";
    }
    if (!it->second.attribution.empty())
      out << indent(depth) << "<attribution>" << escaped(it->second.attribution) << "</attribution>\n";
    if (!it->second.fqdn.empty())
      out << indent(depth) << "<fqdn>" << escaped(it->second.fqdn) << "</fqdn>\n";
    if (!it->second.headers.empty()) {
      out << indent(depth++) << "<headers>\n";
      foreach_const (Profile::headers_t, it->second.headers, hit)
        out << indent(depth) << "<header><name>" << escaped(hit->first) << "</name><value>" << escaped(hit->second) << "</value></header>\n";
      out << indent(--depth) << "</headers>\n";
    }
    out << indent(--depth) << "</profile>\n";
  }
  out << indent(--depth) << "</profiles>\n\n";
  out << indent(--depth) << "</posting>\n";
}

/***
****
***/

ProfilesImpl :: ProfilesImpl (DataIO& data_io):
  _data_io (data_io)
{
  // load from file...
  load (_data_io.get_posting_name());
}

bool
ProfilesImpl :: has_profiles () const
{
  return !profiles.empty();
}

bool
ProfilesImpl :: has_from_header (const StringView& from) const
{
  foreach_const (profiles_t, profiles, it)
    if (from.strstr (it->second.address))
      return true;

  return false;
}

std::set<std::string>
ProfilesImpl :: get_profile_names () const
{
  std::set<std::string> names;
  foreach_const (profiles_t, profiles, it)
    names.insert (it->first);
  return names;
}

bool
ProfilesImpl :: get_profile (const std::string& key, Profile& setme) const
{
  profiles_t::const_iterator it (profiles.find (key));
  const bool found (it != profiles.end());
  if (found)
    setme = it->second;
  return found;
}

void
ProfilesImpl :: delete_profile (const std::string& profile_name)
{
  profiles.erase (profile_name);
  save ();
}

void
ProfilesImpl :: add_profile (const std::string& profile_name, const Profile& profile)
{
  profiles[profile_name] = profile;
  save ();
}

void
ProfilesImpl :: save () const
{
  const std::string f (_data_io.get_posting_name());
  std::ofstream out (f.c_str());
  serialize (out);
  out.close ();
  ::chmod (f.c_str(), 0600);
}

ProfilesImpl :: ~ProfilesImpl ()
{
  save ();
}
