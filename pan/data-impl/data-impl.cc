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

#include <config.h>
#include <glib/gi18n.h>
#include <glib.h> // for g_build_filename
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/time-elapsed.h>
#include "data-impl.h"

#ifdef HAVE_GKR
#if GTK_CHECK_VERSION(3,0,0)
  #define GCR_API_SUBJECT_TO_CHANGE
  #include <libsecret/secret.h>
  #include <gcr/gcr.h>
  #undef GCR_API_SUBJECT_TO_CHANGE
#else
  #include <gnome-keyring-1/gnome-keyring.h>
  #include <gnome-keyring-1/gnome-keyring-memory.h>
#endif /* GTK_CHECK_VERSION(3,0,0) */
#endif

#include <pan/general/null.h>

using namespace pan;

/**
***
**/

namespace
{

  std::string get_cache_path ()
  {
    char * pch (g_build_filename (file::get_pan_home().c_str(), "article-cache", NULL));
    file :: ensure_dir_exists (pch);
    std::string path (pch);
    g_free (pch);
    return path;
  }

  std::string get_encode_cache_path ()
  {
    char * pch (g_build_filename (file::get_pan_home().c_str(), "encode-cache", NULL));
    file :: ensure_dir_exists (pch);
    std::string path (pch);
    g_free (pch);
    return path;
  }

}

DataImpl :: DataImpl (const StringView& cache_ext, Prefs& prefs, bool unit_test, int cache_megs, DataIO * io):
  ProfilesImpl (*io),
  _cache (get_cache_path(), cache_ext, cache_megs),
  _encode_cache (get_encode_cache_path(), cache_megs),
  _certstore(*this),
  _queue(nullptr),
  _unit_test (unit_test),
  _data_io (io),
  _prefs(prefs),
  _descriptions_loaded (false),
  _cached_xover_entry(nullptr),
  _rules_filter (prefs.get_flag("rules-autocache-mark-read", false),
                 prefs.get_flag("rules-auto-dl-mark-read", false),
                 prefs.get_flag("rules-autocache-mark-read", false)),
  newsrc_autosave_id (0),
  newsrc_autosave_timeout (0)
{
  rebuild_backend ();
}

void
DataImpl :: rebuild_backend ()
{
  if (_unit_test)
  {
    debug ("data-impl not loading anything because we're in unit test mode");
  }
  else
  {
    TimeElapsed timer;

    const std::string score_filename (_data_io->get_scorefile_name());
    if (file :: file_exists (score_filename.c_str()))
      _scorefile.parse_file (score_filename);

    load_server_properties (*_data_io);
    load_newsrc_files (*_data_io);
    load_group_xovers (*_data_io);
    load_group_permissions (*_data_io);

    _descriptions.clear ();
    _descriptions_loaded = false;

    //load_group_descriptions (*_data_io);
    Log::add_info_va (_("Loaded data backend in %.1f seconds"), timer.get_seconds_elapsed());
  }
}

DataImpl :: ~DataImpl ()
{
  save_state ();
}

void
DataImpl :: save_state ()
{
  if (!_unit_test)
  {
    debug ("data-impl dtor saving xov, newsrc...");
    save_group_xovers (*_data_io);
    save_newsrc_files (*_data_io);
  }
}

std::string
DataImpl :: get_scorefile_name() const
{
  return _data_io->get_scorefile_name();
}

#ifdef HAVE_GKR
#if GTK_CHECK_VERSION(3,0,0)
gboolean
DataImpl :: password_encrypt (const PasswordData& pw)
{
GError *error_c = NULL;

  return (
    secret_password_store_sync (
      SECRET_SCHEMA_COMPAT_NETWORK,
      SECRET_COLLECTION_DEFAULT,
      _("Pan Newsreader's server passwords"),
      pw.pw,
      NULL, &error_c,
      "user", pw.user.str,
      "server", pw.server.c_str(),
      NULL)
    );

}

gchar*
DataImpl :: password_decrypt (PasswordData& pw) const
{
  GError *error_c = NULL;
  gchar* pwd = NULL;

  pwd =
    secret_password_lookup_sync (
    SECRET_SCHEMA_COMPAT_NETWORK,
    NULL,
    &error_c,
    "user", pw.user.str,
    "server", pw.server.c_str(),
    NULL);

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
#else
GnomeKeyringResult
DataImpl :: password_encrypt (const PasswordData& pw)
{
//  g_return_val_if_fail (pw, GNOME_KEYRING_RESULT_NO_KEYRING_DAEMON);

  return (
    gnome_keyring_store_password_sync (
      GNOME_KEYRING_NETWORK_PASSWORD,
      GNOME_KEYRING_DEFAULT,
      _("Pan Newsreader's server passwords"),
      pw.pw,
      "user", pw.user.str,
      "server", pw.server.c_str(),
      NULL)
    );

}

// TODO use gnome_keyring_memory_new etc
GnomeKeyringResult
DataImpl :: password_decrypt (PasswordData& pw) const
{

  gchar* pwd = NULL;

  GnomeKeyringResult ret =
    gnome_keyring_find_password_sync (
    GNOME_KEYRING_NETWORK_PASSWORD,
    &pwd,
    "user", pw.user.str,
    "server", pw.server.c_str(),
    NULL);

  if (pwd)
  {
    pw.pw = gnome_keyring_memory_strdup(pwd);
    gnome_keyring_free_password(pwd);
  }
  else
  {
    pw.pw = const_cast<gchar*>("");
  }

  return (pw.pw ? GNOME_KEYRING_RESULT_OK : GNOME_KEYRING_RESULT_DENIED) ;
}
#endif /* GTK_CHECK_VERSION(3,0,0) */
#endif
