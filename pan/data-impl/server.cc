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
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
extern "C" {
  #include <glib.h> // for GMarkup
  #include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include <pan/general/messages.h>
#include "data-impl.h"

using namespace pan;

/**
***
**/

void
DataImpl :: delete_server (const Quark& server_in)
{
  const Quark server (server_in);

  if (_servers.count (server))
  {
    const std::string newsrc_filename (_servers[server].newsrc_filename);
    _servers.erase (server);
    save_server_properties (*_data_io);
    std::remove (newsrc_filename.c_str());
    rebuild_backend ();
  }
}

Quark
DataImpl :: add_new_server ()
{
  Quark server;
  for (unsigned long i(1); ; ++i) {
    char buf[64];
    snprintf (buf, sizeof(buf), "%lu", i);
    server = buf;
    if (!_servers.count (server)) {
      _servers[server];
      break;
    }
  }
  return server;
}

DataImpl :: Server*
DataImpl :: find_server (const Quark& server)
{
  Server * retval (0);
  servers_t::iterator it (_servers.find (server));
  if (it != _servers.end())
    retval = &it->second;
  return retval;
}

const DataImpl :: Server*
DataImpl :: find_server (const Quark& server) const
{
  const Server * retval (0);
  servers_t::const_iterator it (_servers.find (server));
  if (it != _servers.end())
    retval = &it->second;
  return retval;
}

void
DataImpl :: set_server_article_expiration_age  (const Quark  & server,
                                                int            days)
{
  Server * s (find_server (server));
  assert (s != 0);

  s->article_expiration_age = std::max (0, days);

  save_server_properties (*_data_io);
}

  
void
DataImpl :: set_server_auth (const Quark       & server,
                             const StringView  & username,
                             const StringView  & password)
{
  Server * s (find_server (server));
  assert (s != 0);

  s->username = username;
  s->password = password;

  save_server_properties (*_data_io);
}

void
DataImpl :: set_server_addr (const Quark       & server,
                             const StringView  & host,
                             int                 port)
{
  Server * s (find_server (server));
  assert (s != 0);
  s->host = host;
  s->port = port;
  save_server_properties (*_data_io);
}


void
DataImpl :: set_server_limits (const Quark   & server,
                               int             max_connections)
{
  Server * s (find_server (server));
  assert (s != 0);
  s->max_connections = max_connections;
  save_server_properties (*_data_io);
}

void
DataImpl :: set_server_rank (const Quark   & server,
                             int             rank)
{
  Server * s (find_server (server));
  assert (s != 0);
  s->rank = rank;
  save_server_properties (*_data_io);
}

bool
DataImpl :: get_server_auth (const Quark   & server,
                             std::string   & setme_username,
                             std::string   & setme_password) const
{
  const Server * s (find_server (server));
  const bool found (s != 0);
  if (found) {
    setme_username = s->username;
    setme_password = s->password;
  }
  return found;
}
                                                                                             
bool
DataImpl :: get_server_addr (const Quark   & server,
                             std::string   & setme_host,
                             int           & setme_port) const
{
  const Server * s (find_server (server));
  const bool found (s != 0);
  if (found) {
    setme_host = s->host;
    setme_port = s->port;
  }
  return found;
}

std::string
DataImpl :: get_server_address (const Quark& server) const
{
  const Server * s (find_server (server));
  return std::string (s ? s->host : "");
}

int
DataImpl :: get_server_limits (const Quark & server) const
{
  int retval (2);
  const Server * s (find_server (server));
  if (s != 0)
    retval = s->max_connections;
  return retval;
}

int
DataImpl :: get_server_rank (const Quark & server) const
{
  int retval (1);
  const Server * s (find_server (server));
  if (s != 0)
    retval = s->rank;
  return retval;
}

int
DataImpl :: get_server_article_expiration_age  (const Quark  & server) const
{
  int retval (31);
  const Server * s (find_server (server));
  if (s != 0)
    retval = s->article_expiration_age;
  return retval;
}

/***
****
***/

namespace
{
  typedef std::map<std::string,std::string> keyvals_t;
  typedef std::map<std::string,keyvals_t> key_to_keyvals_t;

  struct ServerParseContext
  {
    std::string key;
    std::string text;
    key_to_keyvals_t data;
  };

  void start_element (GMarkupParseContext *context,
                      const gchar         *element_name,
                      const gchar        **attribute_names,
                      const gchar        **attribute_vals,
                      gpointer             user_data,
                      GError             **error)
  {
    ServerParseContext& mc (*static_cast<ServerParseContext*>(user_data));

    if (!strcmp (element_name, "server"))
      for (const char **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
        if (!strcmp (*k,"id"))
           mc.key = *v;
  }

  void end_element (GMarkupParseContext *context,
                    const gchar         *element_name,
                    gpointer             user_data,
                    GError             **error)
  {
    ServerParseContext& mc (*static_cast<ServerParseContext*>(user_data));
    if (!mc.key.empty())
      mc.data[mc.key][element_name] = mc.text;
  }

  void text (GMarkupParseContext *context,
             const gchar         *text,
             gsize                text_len,  
             gpointer             user_data,
             GError             **error)
  {
    static_cast<ServerParseContext*>(user_data)->text.assign (text, text_len);
  }

  int to_int (const std::string& s, int default_value=0)
  {
    return s.empty() ? default_value : atoi(s.c_str());
  }
}


void
DataImpl :: load_server_properties (const DataIO& source)
{
  const std::string filename (source.get_server_filename());

  gchar * txt (0);
  gsize len (0);
  g_file_get_contents (filename.c_str(), &txt, &len, 0);

  ServerParseContext spc;
  GMarkupParser p;
  p.start_element = start_element;
  p.end_element = end_element;
  p.text = text;
  p.passthrough = 0;
  p.error = 0;
  GMarkupParseContext* c = g_markup_parse_context_new (&p, (GMarkupParseFlags)0, &spc, 0);
  GError * gerr (0);
  if (txt!=0 && len!=0)
    g_markup_parse_context_parse (c, txt, len, &gerr);
  if (gerr) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), filename.c_str(), gerr->message);
    g_clear_error (&gerr);
  }
  g_markup_parse_context_free (c);
  g_free (txt);

  // populate the servers from the info we loaded...
  _servers.clear ();
  foreach_const (key_to_keyvals_t, spc.data, it) {
    Server& s (_servers[it->first]);
    keyvals_t kv (it->second);
    s.host = kv["host"];
    s.username = kv["username"];
    s.password = kv["password"];
    s.port = to_int (kv["port"], 119);
    s.max_connections = to_int (kv["connection-limit"], 2);
    s.article_expiration_age = to_int(kv["expire-articles-n-days-old"], 31);
    s.rank = to_int(kv["rank"], 1);
    s.newsrc_filename = kv["newsrc"];
    if (s.newsrc_filename.empty()) { // set a default filename
      std::ostringstream o;
      o << file::get_pan_home() << G_DIR_SEPARATOR << "newsrc-" << it->first;
      s.newsrc_filename = o.str ();
    }
  }

  save_server_properties (*const_cast<DataIO*>(&source));
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
DataImpl :: save_server_properties (DataIO& data_io) const
{
  int depth (0);
  std::ostream * out = data_io.write_server_properties ();

  *out << "<?xml version=\"1.0\" encoding=\"utf-8\" ?>\n";

  // sort the servers by id
  typedef std::set<Quark,AlphabeticalQuarkOrdering> alpha_quarks_t;
  alpha_quarks_t servers;
  foreach_const (servers_t, _servers, it)
    servers.insert (it->first);

  // write the servers to the ostream
  *out << indent(depth++) << "<server-properties>\n";
  foreach_const (alpha_quarks_t, servers, it) {
    const Server* s (find_server (*it));
    *out << indent(depth++) << "<server id=\"" << escaped(it->to_string()) << "\">\n";
    *out << indent(depth) << "<host>" << escaped(s->host) << "</host>\n"
         << indent(depth) << "<port>" << s->port << "</port>\n"
         << indent(depth) << "<username>" << escaped(s->username) << "</username>\n"
         << indent(depth) << "<password>" << escaped(s->password) << "</password>\n"
         << indent(depth) << "<expire-articles-n-days-old>" << s->article_expiration_age << "</expire-articles-n-days-old>\n"
         << indent(depth) << "<connection-limit>" << s->max_connections << "</connection-limit>\n"
         << indent(depth) << "<newsrc>" << s->newsrc_filename << "</newsrc>\n"
         << indent(depth) << "<rank>" << s->rank << "</rank>\n";
    *out << indent(--depth) << "</server>\n";
  }
  *out << indent(--depth) << "</server-properties>\n";

  data_io.write_done (out);
}
