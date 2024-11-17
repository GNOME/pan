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

/**************
***************
**************/

#include <SQLiteCpp/Exception.h>
#include <cassert>
#include <config.h>
#include <cstddef>
#include <glib/gi18n.h>
#include <glib.h> // for g_build_filename
#include <gio/gio.h>
#include <log4cxx/logger.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/time-elapsed.h>
#include <sstream>
#include <string>
#include "data-impl.h"
#include "pan/general/log4cxx.h"

#ifdef HAVE_GKR
  #define GCR_API_SUBJECT_TO_CHANGE
  #include <libsecret/secret.h>
  #include <gcr/gcr.h>
  #undef GCR_API_SUBJECT_TO_CHANGE
#endif


using namespace pan;

/**
***
**/

namespace {
log4cxx::LoggerPtr logger(pan::getLogger("server"));

std::string get_cache_path()
{
  char *pch(
    g_build_filename(file::get_pan_home().c_str(), "article-cache", nullptr));
  file ::ensure_dir_exists(pch);
  std::string path(pch);
  g_free(pch);
  return path;
}

std::string get_encode_cache_path()
{
  char *pch(
    g_build_filename(file::get_pan_home().c_str(), "encode-cache", nullptr));
  file ::ensure_dir_exists(pch);
  std::string path(pch);
  g_free(pch);
  return path;
}

} // namespace

namespace pan {

void load_db_schema_file(SQLiteDb &pan_db, char const *file)
{
  GError *error;
  char* contents;
  gsize length;

  // try local path
  gchar *sql_path = g_build_filename((gchar*) "pan/data-impl/sql", file, NULL);
  GFile *sql_file = g_file_new_for_path((char*) sql_path);

  // does local path exists
  if (g_file_query_exists(sql_file, nullptr)) {
    g_file_load_contents(sql_file, nullptr, &contents, &length, nullptr, &error);
  } else {
    // free local data
    g_free(sql_path);
    g_free(sql_file);
    // try system path
    sql_path = g_build_filename((gchar*) PAN_SYSTEM_SQL_PATH, file, NULL);
    sql_file = g_file_new_for_path((char*) sql_path);
  }

  if (g_file_query_exists(sql_file, nullptr)) {
    g_file_load_contents(sql_file, nullptr, &contents, &length, nullptr, &error);
  } else {
    std::cerr << "Fatal error: cannot find SQL schema file " << file << std::endl;
    exit(EXIT_FAILURE);
  }

  pan_db.exec(contents);

  g_free(sql_path);
  g_free(sql_file);
  g_free(contents);
}

void load_db_schema(SQLiteDb &pan_db) {
  // this may not be portable across distributions
  pan_db.loadExtension("/usr/lib/sqlite3/pcre.so", nullptr);

  std::vector<std::string> sql_files { "01-server.sql", "02-group.sql", "03-article.sql" };
  for (int i = 0; i < sql_files.size(); i++) {
    load_db_schema_file(pan_db, sql_files[i].c_str());
  }
}
}

using namespace pan;

DataImpl ::DataImpl(StringView const &cache_ext,
                    Prefs &prefs,
                    SQLiteDb &my_pan_db,
                    bool unit_test,
                    int cache_megs,
                    DataIO *io) :
  ProfilesImpl(*io),
  _cache(get_cache_path(), cache_ext, cache_megs),
  _encode_cache(get_encode_cache_path(), cache_megs),
  _certstore(*this),
  _queue(nullptr),
  pan_db(my_pan_db),
  _unit_test(unit_test),
  _data_io(io),
  _prefs(prefs),
  _cached_xover_entry(nullptr),
  _article_rules(prefs.get_flag("rules-autocache-mark-read", false),
                 prefs.get_flag("rules-auto-dl-mark-read", false))
{
  rebuild_backend ();
}

void
DataImpl :: rebuild_backend ()
{
  load_db_schema(pan_db);

  if (_unit_test)
  {
    pan_debug ("data-impl not loading anything because we're in unit test mode");
  }
  else
  {
    TimeElapsed timer;

    const std::string score_filename (_data_io->get_scorefile_name());
    if (file :: file_exists (score_filename.c_str()))
      _scorefile.parse_file (score_filename);

    rebuild_server_data();
    rebuild_group_data();
    rebuild_group_xover_data();
    rebuild_group_permission_data();

    rebuild_group_description_data();

    Log::add_info_va (_("Loaded data backend in %.1f seconds"), timer.get_seconds_elapsed());
  }
}

void DataImpl ::rebuild_server_data()
{
  quarks_t server_list = get_server_ids_from_db();

  if (server_list.empty()) {
    // load servers from file only if SQL db is empty
    migrate_server_properties_into_db(*_data_io);
    server_list = get_server_ids_from_db();
  }

  // populate the servers from database information into
  // memory. Which is a bit dumb since the DB should be the
  // reference. This is duplicated information. But this cannot be
  // removed until groups are also managed in DB.
  _servers.clear();
  foreach_const (quarks_t, server_list, it)
  {
    LOG4CXX_DEBUG(logger, "loading server with pan_id " << it->c_str() << " from DB");
    Server &server(_servers[it->c_str()]);
    read_server(it->c_str(), &server);
  }
}

void DataImpl ::rebuild_group_data()
{
  // check if DB has group data
  SQLite::Statement group_q(pan_db, R"SQL(
    select count(name) from `group` as g, `server_group` as sg, `server` as s
    where sg.group_id == g.id and sg.server_id == s.id and s.host != "local";
)SQL");
  int group_count = 0;
  while (group_q.executeStep())
  {
    group_count = group_q.getColumn(0);
  }

  if (group_count == 0) {
    // Migrate group data from newsrc files into DB.
    migrate_newsrc_files(*_data_io);
  }

  // now load group data from DB into memory
  load_groups_from_db();
}

void DataImpl ::rebuild_group_description_data()
{
  // check if DB contains group descriptions
  SQLite::Statement group_desc_q(
    pan_db, "select count(description) from `group_description`;");
  int desc_count = 0;
  while (group_desc_q.executeStep())
  {
    desc_count = group_desc_q.getColumn(0);
  }

  if (desc_count == 0)
  {
    // migrate group descriptions into DB
    migrate_group_descriptions(*_data_io);
  }
}

void DataImpl ::rebuild_group_permission_data()
{
  // check if DB contains group descriptions
  SQLite::Statement group_perm_q(
    pan_db, "select count() from `group` where permission != 'y';");
  int count = 0;
  while (group_perm_q.executeStep())
  {
    count = group_perm_q.getColumn(0);
  }

  if (count == 0)
  {
    // migrate group permissions into DB
    migrate_group_permissions(*_data_io);
  }
}

void DataImpl ::rebuild_group_xover_data()
{
  // check if DB contains group xover data
  SQLite::Statement xov_q(
    pan_db, "select count() from `group` where total_article_count is not null limit 1;");
  int count = 0;
  while (xov_q.executeStep())
  {
    count = xov_q.getColumn(0);
  }

  if (count == 0)
    migrate_group_xovers (*_data_io);
}

DataImpl ::~DataImpl()
{
}

std::string
DataImpl :: get_scorefile_name() const
{
  return _data_io->get_scorefile_name();
}

#ifdef HAVE_GKR
gboolean DataImpl ::password_encrypt(PasswordData const &pw)
{
GError *error_c = nullptr;

  return (
    secret_password_store_sync (
      SECRET_SCHEMA_COMPAT_NETWORK,
      SECRET_COLLECTION_DEFAULT,
      _("Pan Newsreader's server passwords"),
      pw.pw,
      nullptr, &error_c,
      "user", pw.user.str,
      "server", pw.server.c_str(),
      nullptr)
    );

}

gchar*
DataImpl :: password_decrypt (PasswordData& pw) const
{
  GError *error_c = nullptr;
  gchar* pwd = nullptr;

  pwd =
    secret_password_lookup_sync (
    SECRET_SCHEMA_COMPAT_NETWORK,
    nullptr,
    &error_c,
    "user", pw.user.str,
    "server", pw.server.c_str(),
    nullptr);

  if (pwd)
  {
    pw.pw = gcr_secure_memory_strdup(pwd);
    secret_password_free(pwd);
  }
  else
  {
    pw.pw = const_cast<gchar*>("");
  }

  return pwd;
}
#endif
