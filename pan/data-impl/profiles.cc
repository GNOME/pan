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
#include <log4cxx/logger.h>
#include <map>
#include <string>
extern "C"
{
#include <sys/stat.h>  // for chmod
#include <sys/types.h> // for chmod
}
#include "data-io.h"
#include "profiles.h"
#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include "pan/general/log4cxx.h"
#include <pan/general/macros.h>
#include <pan/general/string-view.h>

using namespace pan;

///
///  XML Parsing
///
namespace {
typedef std::map<std::string, Profile> profiles_t;

log4cxx::LoggerPtr logger(pan::getLogger("profiles"));

struct MyContext
{
    bool is_active;
    std::string profile_name;
    std::string text;
    std::string header_name;
    profiles_t &profiles;
    std::string &active_profile;

    MyContext(profiles_t &p, std::string &ap) :
      is_active(false),
      profiles(p),
      active_profile(ap)
    {
    }
};

// called for open tags <foo bar='baz'>
void start_element(GMarkupParseContext *context UNUSED,
                   gchar const *element_name_str,
                   gchar const **attribute_names,
                   gchar const **attribute_vals,
                   gpointer user_data,
                   GError **error UNUSED)
{
  MyContext &mc(*static_cast<MyContext *>(user_data));
  std::string const element_name(element_name_str);

  if (element_name == "profile")
  {
    mc.is_active = false;
    for (char const **k(attribute_names), **v(attribute_vals);
         *k && ! mc.is_active;
         ++k, ++v)
    {
      mc.is_active = ! strcmp(*k, "active");
    }
  }

  if (element_name == "profile")
  {
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (! strcmp(*k, "name"))
      {
        mc.profile_name = *v;
      }
    }
    if (! mc.profile_name.empty())
    {
      mc.profiles[mc.profile_name].clear();
    }
  }

  if ((element_name == "signature-file") && ! mc.profile_name.empty())
  {
    Profile &p(mc.profiles[mc.profile_name]);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (! strcmp(*k, "active"))
      {
        p.use_sigfile = ! strcmp(*v, "true");
      }
      else if (! strcmp(*k, "type"))
      {
        if (! strcmp(*v, "file"))
        {
          p.sig_type = p.FILE;
        }
        else if (! strcmp(*v, "command"))
        {
          p.sig_type = p.COMMAND;
        }
        else
        {
          p.sig_type = p.TEXT;
        }
      }
    }
  }
  if ((element_name == "gpg-signature") && ! mc.profile_name.empty())
  {
    Profile &p(mc.profiles[mc.profile_name]);
    for (char const **k(attribute_names), **v(attribute_vals); *k; ++k, ++v)
    {
      if (! strcmp(*k, "active"))
      {
        p.use_sigfile = ! strcmp(*v, "true");
      }
      p.sig_type = p.GPGSIG;
      p.use_gpgsig = true;
    }
  }
}

// Called for close tags </foo>
void end_element(GMarkupParseContext *context UNUSED,
                 gchar const *element_name_str,
                 gpointer user_data,
                 GError **error UNUSED)
{
  MyContext &mc(*static_cast<MyContext *>(user_data));
  std::string const element_name(element_name_str);
  StringView t(mc.text);
  t.trim();

  if (! mc.profile_name.empty())
  {
    Profile &p(mc.profiles[mc.profile_name]);
    if (element_name == "signature-file")
    {
      p.signature_file.assign(t.str, t.len);
    }
    else if (element_name == "gpg-signature")
    {
      p.gpg_sig_uid.assign(t.str, t.len);
    }
    else if (element_name == "attribution")
    {
      p.attribution.assign(t.str, t.len);
    }
    else if (element_name == "fqdn")
    {
      p.fqdn.assign(t.str, t.len);
    }
    else if (element_name == "face")
    {
      p.face.assign(t.str, t.len);
    }
    else if (element_name == "xface")
    {
      p.xface.assign(t.str, t.len);
    }
    else if (element_name == "username")
    {
      p.username.assign(t.str, t.len);
    }
    else if (element_name == "address")
    {
      p.address.assign(t.str, t.len);
    }
    else if (element_name == "server")
    {
      p.posting_server = t;
    }
    else if (element_name == "profile" && mc.is_active)
    {
      mc.active_profile.assign(t.str, t.len);
    }
    else if (element_name == "name")
    {
      mc.header_name.assign(t.str, t.len);
    }
    else if ((element_name == "value") && ! mc.header_name.empty())
    {
      p.headers[mc.header_name].assign(t.str, t.len);
    }
  }
}

void text(GMarkupParseContext *context UNUSED,
          gchar const *text,
          gsize text_len,
          gpointer user_data,
          GError **error UNUSED)
{
  static_cast<MyContext *>(user_data)->text.assign(text, text_len);
}

} // namespace

void ProfilesImpl ::clear()
{
  profiles.clear();
  active_profile.clear();
}

bool ProfilesImpl ::migrate_posting_profiles(std::string const &filename)
{
  std::string txt;
  if (file ::get_text_file_contents(filename, txt))
  {
    LOG4CXX_INFO(logger, "Migrating posting profiles...");

    MyContext mc(profiles, active_profile);
    GMarkupParser p;
    p.start_element = start_element;
    p.end_element = end_element;
    p.text = text;
    p.passthrough = nullptr;
    p.error = nullptr;
    GError *gerr(nullptr);
    GMarkupParseContext *c(
      g_markup_parse_context_new(&p, (GMarkupParseFlags)0, &mc, nullptr));
    g_markup_parse_context_parse(c, txt.c_str(), txt.size(), &gerr);
    if (gerr)
    {
      Log::add_err_va(_("Error reading file \"%s\": %s"),
                      filename.data(),
                      gerr->message);
      g_clear_error(&gerr);
      return false;
    }
    g_markup_parse_context_free(c);

    LOG4CXX_DEBUG(logger, "Removing " << filename << "...");
    std::remove(filename.data());

    return true;
  }
  return false;
}

void ProfilesImpl ::load_posting_profiles()
{
  SQLite::Statement read(pan_db, R"SQL(
    select p.name, s.host, a.author, face, xface, attribution, fqdn
    from profile as p
    join author as a on p.author_id == a.id
    join server as s on p.server_id == s.id
  )SQL");

  SQLite::Statement read_header(pan_db, R"SQL(
    select name, value from profile_header
    where profile_id = (select id from profile where name = ?)
  )SQL");

  SQLite::Statement read_signature(pan_db, R"SQL(
    select type, active, content, gpg_sig_uid from signature
    where profile_id = (select id from profile where name = ?)
  )SQL");

  while (read.executeStep())
  {
    int i(0);
    std::string name(read.getColumn(i++).getText());
    LOG4CXX_DEBUG(logger, "Reading posting profile " << name);

    Profile &p(profiles[name]);
    p.posting_server = Quark(read.getColumn(i++).getText());
    StringView auth_name, auth_address, author( read.getColumn(i++).getText());

    author.trim();
    if (author.strrchr('<'))
    {
      author.pop_token(auth_name, '<');
      auth_name.trim();
      p.username = auth_name;
      author.pop_token(auth_address, '>');
      auth_address.trim();
      p.address = auth_address;
    }
    else if (author.strrchr('@'))
    {
      p.username.clear();
      p.address = author;
    }
    else
    {
      p.address.clear();
      p.username = author;
    }

    p.face = read.getColumn(i++).getText();
    p.xface = read.getColumn(i++).getText();
    p.attribution = read.getColumn(i++).getText();
    p.fqdn = read.getColumn(i++).getText();

    read_header.reset();
    read_header.bind(1, name);
    while (read_header.executeStep())
    {
      std::string key(read_header.getColumn(0).getText());
      p.headers[key] = read_header.getColumn(1).getText();
    }

    read_signature.reset();
    read_signature.bind(1, name);
    while (read_signature.executeStep())
    {
      std::string type(read_signature.getColumn(0).getText());
      p.use_gpgsig = false;
      if (type == "gpgsig")
      {
        p.use_gpgsig = true;
        p.sig_type = p.GPGSIG;
      }
      else if (type == "text")
        p.sig_type = p.TEXT;
      else if (type == "command")
        p.sig_type = p.COMMAND;
      else // file
        p.sig_type = p.FILE;

      p.use_sigfile = read_signature.getColumn(1).getInt();
      p.signature_file = read_signature.getColumn(2).getText();
      p.gpg_sig_uid = read_signature.getColumn(3).getText();
    }
  }
}

/***
****
***/

ProfilesImpl ::ProfilesImpl(DataIO &data_io) :
  _data_io(data_io)
{
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
  save_posting_profiles ();
}

void
ProfilesImpl :: add_profile (const std::string& profile_name, const Profile& profile)
{
  profiles[profile_name] = profile;
  save_posting_profiles ();
}

void
ProfilesImpl :: save_posting_profiles () const
{
  SQLite::Statement save_author(pan_db, R"SQL(
    insert into author (author) values ($a)
    on conflict do nothing;
  )SQL");

  SQLite::Statement save(pan_db, R"SQL(
    insert into profile (name, server_id, author_id, face, xface, attribution, fqdn)
    values ($n, $s_id, (select id from author where author == $author), $f, $xf, $atr, $fqdn)
    on conflict (name)
    do update set author_id = (select id from author where author == $author),
              server_id = $s_id, face= $f, xface = $xf, attribution = $atr, fqdn = $fqdn
    where name = $name
  )SQL");

  SQLite::Statement delete_header(pan_db, R"SQL(
    delete from profile_header
    where profile_id = (select id from profile where name = $pf_name)
  )SQL");

  SQLite::Statement save_header(pan_db, R"SQL(
    insert into profile_header (profile_id, name, value)
    values ((select id from profile where name = $pf_name), ?,?)
  )SQL");

  SQLite::Statement save_sig(pan_db, R"SQL(
    insert into signature
    (profile_id, active, type, content, gpg_sig_uid)
    values (
      (select id from profile where name = $pf_name),
      $a, $t, $c, $gpg)
    on conflict (profile_id)
    do update set ( active, type, content, gpg_sig_uid) = ($a, $t, $c, $gpg)
    where profile_id = (select id from profile where name = $pf_name)
  )SQL");

  foreach_const (profiles_t, profiles, it)
  {
    std::string author(it->second.username + " <" + it->second.address + ">");
    save_author.reset();
    save_author.bind(1, author);
    save_author.exec();

    save.reset();
    int i(1);
    save.bind(i++, it->first);
    save.bind(i++, it->second.posting_server.to_view());
    save.bind(i++, author);
    save.bind(i++, it->second.face);
    save.bind(i++, it->second.xface);
    save.bind(i++, it->second.attribution);
    save.bind(i++, it->second.fqdn);
    save.exec();

    delete_header.reset();
    delete_header.bind(1, it->first);
    delete_header.exec();

    foreach_const (Profile::headers_t, it->second.headers, hit)
    {
      save_header.reset();
      save_header.bind(1, it->first);
      save_header.bind(2, hit->first);
      save_header.bind(3, hit->second);
      save_header.exec();
    }

    save_sig.reset();
    i = 1;
    bool active(it->second.use_sigfile);
    save_sig.bind(i++, it->first);
    save_sig.bind(i++, active);
    switch (it->second.sig_type)
    {
      case Profile::FILE:
        save_sig.bind(i++, "file");
        break;
      case Profile::COMMAND:
        save_sig.bind(i++, "command");
        break;
      case Profile::GPGSIG:
        save_sig.bind(i++, "gpgsig");
        break;
      default:
        save_sig.bind(i++, "text");
        break;
    }
    save_sig.bind(i++, it->second.signature_file); // content
    save_sig.bind(i++, it->second.gpg_sig_uid);
    save_sig.exec();
  }
}

ProfilesImpl :: ~ProfilesImpl ()
{
  save_posting_profiles ();
  delete &_data_io;     // we own the DataIO
}
