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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <map>
#include <set>
#include <vector>
#include <glib.h> // for GMarkup
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include "data-impl.h"

#ifdef HAVE_GKR
  #define USE_LIBSECRET_DEFAULT true
#else
  #define USE_LIBSECRET_DEFAULT false
#endif

using namespace pan;

/**
***
**/

void DataImpl ::delete_server(Quark const &server_in)
{
  const Quark server (server_in);

  if (_servers.count (server))
  {
    const std::string newsrc_filename (file::absolute_fn("",_servers[server].newsrc_filename));
    _servers.erase (server);
    save_server_properties (*_data_io, _prefs);
    std::remove (newsrc_filename.c_str());
    rebuild_backend ();
  }
}

Quark
DataImpl :: add_new_server ()
{
  // find a server ID that's not in use
  Quark new_server;
  for (unsigned long i(1); ; ++i) {
    char buf[64];
    snprintf (buf, sizeof(buf), "%lu", i);
    new_server = buf;
    if (!_servers.count (new_server))
      break;
  }

  // add it to the _servers map and give it a default filename
  std::ostringstream o;
  o << "newsrc-" << new_server;
  _servers[new_server].newsrc_filename = o.str ();
  return new_server;
}

quarks_t DataImpl ::get_servers () const {
  quarks_t servers;
  foreach_const (servers_t, _servers, it)
    servers.insert (it->first);
  return servers;
}

Data ::Server *DataImpl ::find_server(Quark const &server)
{
  Server * retval (nullptr);

  servers_t::iterator it (_servers.find (server));
  if (it != _servers.end())
    retval = &it->second;
  return retval;
}

Data ::Server const *DataImpl ::find_server(Quark const &server) const
{
  Server const *retval(nullptr);

  servers_t::const_iterator it (_servers.find (server));
  if (it != _servers.end())
    retval = &it->second;
  return retval;
}

bool DataImpl ::find_server_by_host_name(std::string const &server,
                                         Quark &setme) const
{
  foreach_const(servers_t, _servers, it)
    if (it->second.host == server) { setme = it->first; return true; }
  return false;
}

void DataImpl ::set_server_article_expiration_age(Quark const &server, int days)
{
  Server * s (find_server (server));
  assert (s);

  s->article_expiration_age = std::max (0, days);

}

void DataImpl ::set_server_auth(Quark const &server,
                                StringView const &username,
                                gchar *&password,
                                bool use_gkr)
{
  Server * s (find_server (server));
  assert (s);

  s->username = username;
#ifdef HAVE_GKR
  if (use_gkr)
  {
    PasswordData pw;
    pw.server = s->host;
    pw.user = username;
    pw.pw = password;
    password_encrypt(pw);
  }
  else
  {
    s->password = password;
  }
#else
  s->password = password;
#endif

}

void DataImpl ::set_server_trust(Quark const &server, int const setme)
{
  Server * s (find_server (server));
  assert (s);
  s->trust = setme;
}

void DataImpl ::set_server_compression_type(Quark const &server,
                                            int const setme)
{
  Server * s (find_server (server));
  assert (s);
  s->compression_type = setme;
}

void DataImpl ::set_server_addr(Quark const &server,
                                StringView const &host,
                                int port)
{
  Server * s (find_server (server));
  assert (s);
  s->host = host;
  s->port = port;
}

void DataImpl ::set_server_limits(Quark const &server, int max_connections)
{
  Server * s (find_server (server));
  assert (s);
  s->max_connections = max_connections;

}

void DataImpl ::set_server_rank(Quark const &server, int rank)
{
  Server * s (find_server (server));
  assert (s);
  s->rank = rank;

}

void DataImpl ::set_server_ssl_support(Quark const &server, int ssl)
{
  Server * s (find_server (server));
  assert (s);
  s->ssl_support = ssl;

}

void DataImpl ::set_server_cert(Quark const &server, StringView const &cert)
{

  Server * s (find_server (server));
  assert (s);
  s->cert = cert;

}

void DataImpl ::save_server_info(Quark const &server)
{
  Server * s (find_server (server));
  assert (s);
  save_server_properties (*_data_io, _prefs);
}

bool DataImpl ::get_server_auth(Quark const &server,
                                std::string &setme_username,
                                gchar *&setme_password,
                                bool use_gkr)
{
  Server * s (find_server (server));
  bool found (s);
  if (found) {
    setme_username = s->username;
#ifdef HAVE_GKR
    if (!use_gkr)
    {
      setme_password = g_strdup(s->password.c_str());
    }
    else if (s->gkr_pw)
    {
      setme_password = s->gkr_pw;
    }
    else if (s->username.empty()) {
      // no username, no need for password
      setme_password = (gchar*)"";
    }
    else
    {
      PasswordData pw;
      pw.server = s->host;
      pw.user = s->username;

      if (password_decrypt(pw) == NULL)
      {
        Log::add_urgent_va (_("Received no password from libsecret for server %s."), s->host.c_str());
      }
      else
      {
          setme_password = pw.pw;
          s->gkr_pw = pw.pw;
      }
    }
#else
    setme_password = g_strdup(s->password.c_str());
#endif
  }

  return found;

}

bool DataImpl ::get_server_trust(Quark const &server, int &setme) const
{
  Server const *s(find_server(server));
  bool const found(s);
  if (found) {
    setme = s->trust;
  }

  return found;
}

namespace
{
  CompressionType get_compression_type(int val)
  {
    CompressionType ret = HEADER_COMPRESS_NONE;
    switch (val)
    {
      case 1:
        ret = HEADER_COMPRESS_XZVER;
        break;

      case 2:
        ret = HEADER_COMPRESS_XFEATURE;
        break;

      case 3:
        ret = HEADER_COMPRESS_DIABLO;
        break;
    }
    return ret;
  }
}

bool DataImpl ::get_server_compression_type(Quark const &server,
                                            CompressionType &setme) const
{
    Server const *s(find_server(server));
    bool const found(s);
    if (found)
      setme = get_compression_type(s->compression_type);

    return found;
}

bool DataImpl ::get_server_addr(Quark const &server,
                                std::string &setme_host,
                                int &setme_port) const
{
    Server const *s(find_server(server));
    bool const found(s);
    if (found)
    {
      setme_host = s->host;
      setme_port = s->port;
  }

  return found;

}

std::string DataImpl ::get_server_address(Quark const &server) const
{
  std::string str;
  Server const *s(find_server(server));
  if (s) {
    std::ostringstream x(s->host,std::ios_base::ate);
    x << ":" << s->port;
    str = x.str();
  }

  return str;

}

bool DataImpl ::get_server_ssl_support(Quark const &server) const
{
  bool retval (false);
  Server const *s(find_server(server));
  if (s)
    retval = (s->ssl_support != 0);

  return retval;

}

std::string DataImpl ::get_server_cert(Quark const &server) const
{
  std::string str;
  Server const *s(find_server(server));
  if (s)
    str = s->cert;

  return str;

}

int DataImpl ::get_server_limits(Quark const &server) const
{
  int retval (2);
  Server const *s(find_server(server));
  if (s)
    retval = s->max_connections;

  return retval;

}

int DataImpl ::get_server_rank(Quark const &server) const
{
  int retval (1);
  Server const *s(find_server(server));
  if (s)
    retval = s->rank;

  return retval;

}

int DataImpl ::get_server_article_expiration_age(Quark const &server) const
{
  int retval (31);
  Server const *s(find_server(server));
  if (s)
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

  void start_element(GMarkupParseContext *context UNUSED,
                     gchar const *element_name,
                     gchar const **attribute_names,
                     gchar const **attribute_vals,
                     gpointer user_data,
                     GError **error UNUSED)
  {
    ServerParseContext& mc (*static_cast<ServerParseContext*>(user_data));

    if (!strcmp (element_name, "server"))
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
        if (!strcmp (*k,"id"))
          mc.key = *v;
    }
  }

  void end_element(GMarkupParseContext *context UNUSED,
                   gchar const *element_name,
                   gpointer user_data,
                   GError **error UNUSED)
  {
    ServerParseContext& mc (*static_cast<ServerParseContext*>(user_data));
    if (!mc.key.empty())
      mc.data[mc.key][element_name] = mc.text;
  }

  void text(GMarkupParseContext *context UNUSED,
            gchar const *text,
            gsize text_len,
            gpointer user_data,
            GError **error UNUSED)
  {
    static_cast<ServerParseContext*>(user_data)->text.assign (text, text_len);
  }

  int to_int(std::string const &s, int default_value = 0)
  {
    return s.empty() ? default_value : atoi(s.c_str());
  }
  }

  void DataImpl ::load_server_properties(DataIO const &source)
  {
    const std::string filename(source.get_server_filename());

    std::string txt;
    file ::get_text_file_contents(filename, txt);

    ServerParseContext spc;
    GMarkupParser p;
    p.start_element = start_element;
    p.end_element = end_element;
    p.text = text;
    p.passthrough = nullptr;
    p.error = nullptr;
    GMarkupParseContext *c =
      g_markup_parse_context_new(&p, (GMarkupParseFlags)0, &spc, nullptr);
    GError *gerr(nullptr);
    if (! txt.empty())
      g_markup_parse_context_parse(c, txt.c_str(), txt.size(), &gerr);
    if (gerr)
    {
      Log::add_err_va(
        _("Error reading file \"%s\": %s"), filename.c_str(), gerr->message);
      g_clear_error(&gerr);
    }
    g_markup_parse_context_free(c);

    // populate the servers from the info we loaded...
    _servers.clear();
    foreach_const (key_to_keyvals_t, spc.data, it)
    {
      Server &s(_servers[it->first]);
      keyvals_t kv(it->second);
      s.host = kv["host"];
      s.username = kv["username"];
#ifdef HAVE_GKR
      if (! _prefs.get_flag("use-password-storage", USE_LIBSECRET_DEFAULT))
        s.password = kv["password"];
#else
      s.password = kv["password"];
#endif
      s.port = to_int(kv["port"], STD_NNTP_PORT);
      s.max_connections = to_int(kv["connection-limit"], 2);
      s.article_expiration_age = to_int(kv["expire-articles-n-days-old"], 31);
      s.rank = to_int(kv["rank"], 1);
      int ssl(to_int(kv["use-ssl"], 0));
      s.ssl_support = ssl;
      s.cert = kv["cert"];
      int trust(to_int(kv["trust"], 0));
      s.trust = trust;
      s.compression_type = to_int(kv["compression-type"], 0); // NONE
      s.newsrc_filename = kv["newsrc"];
      if (s.newsrc_filename.empty())
      { // set a default filename
        std::ostringstream o;
        o << "newsrc-" << it->first;
        s.newsrc_filename = o.str();
      }
    }

}

namespace
{
int const indent_char_len(2);

std::string indent(int depth)
{
    return std::string(depth * indent_char_len, ' '); }

std::string escaped(std::string const &s)
{
    char * pch = g_markup_escape_text (s.c_str(), s.size());
    const std::string ret (pch);
    g_free (pch);
    return ret;
  }
}

void
DataImpl :: save_server_properties (DataIO& data_io, Prefs& prefs)
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
    std::string user;
    gchar* pass(NULL);
    get_server_auth(*it, user, pass, prefs.get_flag("use-password-storage", USE_LIBSECRET_DEFAULT));
    *out << indent(depth++) << "<server id=\"" << escaped(it->to_string()) << "\">\n";
    *out << indent(depth) << "<host>" << escaped(s->host) << "</host>\n"
         << indent(depth) << "<port>" << s->port << "</port>\n"
         << indent(depth) << "<username>" << escaped(user) << "</username>\n";
#ifdef HAVE_GKR
    if (prefs.get_flag("use-password-storage", USE_LIBSECRET_DEFAULT))
      *out << indent(depth) << "<password>" << "HANDLED_BY_PASSWORD_STORAGE" << "</password>\n";
    else
      *out << indent(depth) << "<password>" << escaped(pass) << "</password>\n";
#else
    *out << indent(depth) << "<password>" << escaped(pass) << "</password>\n";
#endif
    *out << indent(depth) << "<expire-articles-n-days-old>" << s->article_expiration_age << "</expire-articles-n-days-old>\n"
         << indent(depth) << "<connection-limit>" << s->max_connections << "</connection-limit>\n"
         << indent(depth) << "<newsrc>" << s->newsrc_filename << "</newsrc>\n"
         << indent(depth) << "<rank>" << s->rank << "</rank>\n"
         << indent(depth) << "<use-ssl>" << s->ssl_support << "</use-ssl>\n"
         << indent(depth) << "<trust>" << s->trust << "</trust>\n"
         << indent(depth) << "<compression-type>" << s->compression_type << "</compression-type>\n"
         << indent(depth) << "<cert>"    << s->cert << "</cert>\n";

    *out << indent(--depth) << "</server>\n";
  }
  *out << indent(--depth) << "</server-properties>\n";

  data_io.write_done (out);
}
