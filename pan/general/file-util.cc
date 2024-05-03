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
#include <cassert>
#include <cerrno>
#include <cctype>

#include <glib.h>

extern "C"
{
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>
  #ifndef G_OS_WIN32
    #include <pwd.h>
  #endif
}
#include <glib/gi18n.h>

#include "debug.h"
#include "log.h"
#include "messages.h"
#include "file-util.h"
#include "e-util.h"
#include "utf8-utils.h"
#include <algorithm>


using namespace pan;

#define is_nonempty_string(a) ((a) && (*a))
#define NL std::endl

std::ostream&
file :: print_file_info (std::ostream& os, const char* file)
{
  EvolutionDateMaker dm;
  struct stat sb;
  int ret = stat(file,&sb);

  os << "File information for file "<<file<<NL;
  if (ret)
  {
    os << "File not found / accessible!"<<NL;
    return os;
  }
  os << "Umask : "<<sb.st_mode<<NL;
  os << "User ID : "<< sb.st_uid<<NL;
  os << "Group ID : "<< sb.st_gid<<NL;
  os << "Size (Bytes) : "<<sb.st_size<<NL;
  os << "Last accessed : "<<dm.get_date_string(sb.st_atime)<<NL;
  os << "Last modified : "<<dm.get_date_string(sb.st_mtime)<<NL;
  os << "Last status change : "<<dm.get_date_string(sb.st_ctime)<<NL;

  return os;
}

/***
****
***/

const std::string &
file :: get_pan_home ()
{
  static std::string pan_home;
  if (pan_home.empty())
  {
    const char * env_str = g_getenv ("PAN_HOME");
    if (env_str && *env_str)
      pan_home = env_str;
    else {
      char * pch = g_build_filename (g_get_home_dir(), ".pan2", nullptr);
      pan_home = pch;
      g_free (pch);
    }
  }

  ensure_dir_exists (pan_home);
  return pan_home;
}

std::string
file :: absolute_fn(const std::string &dir, const std::string &base)
{
const char *fn = base.c_str();
if (g_path_is_absolute(fn))
  return base;
const char *ph = file::get_pan_home().c_str();
char *temp = g_build_filename(ph, dir.empty() ? "" : dir.c_str(), fn, nullptr);
std::string out(temp);
g_free(temp);
return out;
}

const char*
file :: pan_strerror (int error_number)
{
  const char * pch (g_strerror (error_number));
  return pch && *pch ? pch : "";
}

namespace
{

  enum EX_ERRORS
  {
    EX_NOFILE, EX_BIT, EX_SUCCESS
  };

  EX_ERRORS check_executable_bit(const char* d)
  {
#ifndef G_OS_WIN32
    struct stat sb;
    if (stat (d, &sb) == -1) return EX_NOFILE;
    const char* user(g_get_user_name());
    struct passwd* pw(getpwnam(user));
    if ((sb.st_mode & S_IXUSR) || ((sb.st_mode & S_IXGRP ) && pw->pw_gid == sb.st_gid))
      return EX_SUCCESS;
    return EX_BIT;
#else
    return EX_SUCCESS;
#endif
  }
}

bool
file :: ensure_dir_exists (const StringView& dirname_sv)
{
  assert (!dirname_sv.empty());

  pan_return_val_if_fail (!dirname_sv.empty(), true);
  bool retval (true);
  const std::string dirname (dirname_sv.to_string());
  EX_ERRORS cmd (check_executable_bit(dirname.c_str()));
  if (cmd == EX_BIT) goto _set_bit;

  if (!g_file_test (dirname.c_str(), G_FILE_TEST_IS_DIR))
    retval = !g_mkdir_with_parents (dirname.c_str(), 0740); // changed from 755

  if (!retval)
  {
    // check for executable bit
    Log::add_err_va("Error creating directory '%s' : %s", dirname.c_str(),
                    cmd == EX_NOFILE ? "error accessing file." : "executable bit not set.");
    // set it manually
    _set_bit:
    if (cmd == EX_BIT)
      if (chmod(dirname.c_str(), 0740))
      {
        Log::add_urgent_va("Error setting executable bit for directory '%s' : "
                           "Please check your permissions.", dirname.c_str());
      }
  }
  return retval;
}

bool
file :: file_exists (const char * filename)
{
   return filename && *filename && g_file_test (filename, G_FILE_TEST_EXISTS);
}


/**
*** Attempt to make a filename safe for use.
*** This is done by replacing illegal characters with '_'.
*** This function assumes the input is UTF8 since gmime uses UTF8 interface.
*** return value must be g_free'd.
**/
std::string
file :: sanitize (const StringView& fname)
{
  std::string ret;

  // sanity checks
  pan_return_val_if_fail (!fname.empty(), ret);

  ret = content_to_utf8(fname);

  // strip illegal characters
# ifdef G_OS_WIN32
  static const char* illegal_chars = "/\\:?*\"\'<>|";
# else
  static const char* illegal_chars = "/\\";
# endif
  for (const char *pch(illegal_chars); *pch; ++pch)
    std::replace (ret.begin(), ret.end(), *pch, '_');

  return ret;
}

char*
file :: normalize_inplace (char * filename)
{
  char *in, *out;
  pan_return_val_if_fail (filename && *filename, filename);

  for (in=out=filename; *in; )
    if (in[0]==G_DIR_SEPARATOR && in[1]==G_DIR_SEPARATOR)
      ++in;
    else
      *out++ = *in++;
  *out = '\0';

  return filename;
}

/**
*** Makes a unique filename given an optional path and a starting file name.
*** The filename is sanitized before looking for uniqueness.
**/
char*
file :: get_unique_fname ( const gchar *path, const gchar *fname)
{
   // sanity checks
   pan_return_val_if_fail (is_nonempty_string (fname), nullptr);

   // sanitize filename
   std::string tmp = sanitize (fname);
   GString * filename = g_string_new_len (tmp.c_str(), tmp.size());

   // add the directory & look for uniqueness
   const char * front = filename->str;
   const char * lastdot = strrchr (front, '.');
   char * lead;
   char * tail;
   if (lastdot == nullptr) {
      lead = g_strdup (front);
      tail = g_strdup ("");
   } else {
      lead = g_strndup (front, lastdot-front);
      tail = g_strdup (lastdot);
   }

   for (int i=1;; ++i)
   {
      char * unique;

      if (i==1 && is_nonempty_string(path))
      {
         unique = g_strdup_printf ("%s%c%s%s",
                             path, G_DIR_SEPARATOR,
                             lead, tail);
      }
      else if (i==1)
      {
         unique = g_strdup_printf ("%s%s", lead, tail);
      }
      else if (is_nonempty_string(path))
      {
         unique = g_strdup_printf ("%s%c%s_copy_%d%s",
                             path, G_DIR_SEPARATOR,
                             lead, i, tail);
      }
      else
      {
         unique = g_strdup_printf ("%s_copy_%d%s", lead, i, tail);
      }

      if (!file_exists (unique)) {
         g_string_assign (filename, unique);
         g_free (unique);
         break;
      }

      g_free (unique);
   }

   /* cleanup */
   g_free (lead);
   g_free (tail);

   return normalize_inplace (g_string_free (filename, FALSE));
}

/***
****
***/

bool
file :: get_text_file_contents (const StringView  & filename,
                                std::string       & setme,
                                const char        * fallback_charset_1,
                                const char        * fallback_charset_2)
{
  // read in the file...
  char * body (nullptr);
  gsize body_len (0);
  GError * err (nullptr);
  const std::string fname (filename.str, filename.len);
  g_file_get_contents (fname.c_str(), &body, &body_len, &err);
  if (err) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), err->message, g_strerror(errno));
    g_clear_error (&err);
    return false;
  }

  // CRLF => LF
  body_len = std::remove (body, body+body_len, '\r') - body;

  // utf8
  setme = content_to_utf8  (StringView (body, body_len),
                            fallback_charset_1,
                            fallback_charset_2);

  g_free (body);
  return true;
}
