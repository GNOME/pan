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

#include "data-io.h"

#include <config.h>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <istream>
#include <map>
#include <ostream>
#include <regex>

extern "C" {
  #include <sys/types.h> // for chmod
  #include <sys/stat.h> // for chmod
  #include <unistd.h>
}

#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/line-reader.h>
#include <pan/general/log.h>


namespace pan {

namespace
{
  std::string get_pan_home_file (const char * fname)
  {
    const std::string home (file::get_pan_home());
    char * filename (g_build_filename (home.c_str(), fname, nullptr));
    std::string retval (filename);
    g_free (filename);
    return retval;
  }

  std::string get_tasks_filename ()
  {
    return get_pan_home_file ("tasks.nzb");
  }

  std::string get_group_descriptions_filename ()
  {
    return get_pan_home_file ("newsgroups.dsc");
  }

  std::string get_group_permissions_filename ()
  {
    return get_pan_home_file ("newsgroups.ynm");
  }

  std::string get_group_xovers_filename ()
  {
    return get_pan_home_file ("newsgroups.xov");
  }

  bool is_reserved_char(char c)
  {
#ifdef G_OS_WIN32
    //Windows filing systems aren't case sensitive. But there are groups out
    //there with upper case names... I can't help thinking some system should
    //validate group names for sanity but apparently no such thing.
    //You could get rid of this if you ensured people ran
    //Enable-WindowsOptionalFeature -Online -FeatureName Microsoft-Windows-Subsystem-Linux
    //fsutil.exe file SetCaseSensitiveInfo <path to groups> enable
    //which for now I think is unlikely.
    if ('A' <= c && c <= 'Z')
    {
      return true;
    }
#endif
    switch (c)
    {
      case '%': //Reserve % as we use it to escape things.
      case '/':
#ifdef G_OS_WIN32
      case '<':
      case '>':
      case ':':
      case '"':
      case '\\':
      case '|':
      case '?':
      case '*':
#endif
        return true;

      default:
        return false;
    }
  }

  char to_hex_char(char c)
  {
    return c < 10 ? c + '0' : c + 'A' - 10;
  }

  std::string get_group_headers_filename (const Quark& group)
  {
    const std::string home (file::get_pan_home());
    //A note. We do a lot of work encoding the names here, because
    //1) Windows is case-insensitive, and there are groups whose names differ
    //   only in case (uk.legaL, uk.Legal, uk.legal)
    //2) There are certain names you can't use on windows and there is a group
    //   called con.politics
    //3) There are groups with '/' in the group name...
    std::string encoded_group;
    for (auto c : group.to_string())
    {
      if (is_reserved_char(c))
      {
        encoded_group += "%";
        encoded_group += to_hex_char((c >> 4) & 0xf);
        encoded_group += to_hex_char(c & 0xf);
      }
      else
      {
        encoded_group += c;
      }
    }
#ifdef G_OS_WIN32
    static std::regex reserved("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9])\\.",
                               std::regex::extended | std::regex::icase);

    encoded_group = std::regex_replace(encoded_group, reserved, "$1%2E");

    //Sigh - and finally we can't deal with a trailing '.' either
    encoded_group = std::regex_replace(encoded_group,
                                       std::regex("\\.$", std::regex::extended),
                                       "%2E");
#endif

    char * filename (g_build_filename (home.c_str(), "groups", encoded_group.c_str(), nullptr));
    char * dirname (g_path_get_dirname (filename));
    file :: ensure_dir_exists (dirname);
    std::string retval (filename);
    g_free (dirname);
    g_free (filename);
    return retval;
  }

  std::string get_download_stats_filename ()
  {
    return get_pan_home_file ("downloads.stats");
  }

}

std::string
DataIO :: get_scorefile_name () const
{
  std::string s;

  const char * env_str (g_getenv ("SCOREFILE"));
  if (env_str && *env_str)
    s = env_str;

  if (s.empty()) {
    char * path (g_build_filename (g_get_home_dir(), "News", "Score", nullptr));
    if (file :: file_exists (path))
      s = path;
    g_free (path);
  }

  if (s.empty())
    s = get_pan_home_file ("Score");

  return s;
}

std::string
DataIO :: get_posting_name () const
{
  return get_pan_home_file ("posting.xml");
}

std::string
DataIO :: get_server_filename () const
{
  return get_pan_home_file ("servers.xml");
}

/****
*****
****/

void
DataIO :: clear_group_headers (const Quark& group)
{
  const std::string filename (get_group_headers_filename (group));
  std::remove (filename.c_str());
}

/****
*****
****/

LineReader*
DataIO :: read_tasks () const
{
  const std::string filename (get_tasks_filename ());
  return file::file_exists(filename.c_str()) ? read_file(filename) : nullptr;
}

LineReader*
DataIO :: read_group_descriptions () const
{
   return read_file (get_group_descriptions_filename ());
}

LineReader*
DataIO :: read_group_permissions () const
{
   return read_file (get_group_permissions_filename ());
}

LineReader*
DataIO :: read_download_stats () const
{
   return read_file (get_download_stats_filename ());
}

LineReader*
DataIO :: read_group_xovers () const
{
  return read_file (get_group_xovers_filename ());
}

LineReader*
DataIO :: read_group_headers (const Quark& group) const
{
  const std::string filename (get_group_headers_filename (group));
  return file::file_exists(filename.c_str()) ? read_file(filename) : nullptr;
}

LineReader*
DataIO :: read_file (const StringView& filename) const
{
  return new FileLineReader (filename);
}

/****
*****
****/

namespace
{
  std::map<std::ofstream*, std::string> ostream_to_filename;

  std::ostream* get_ostream (const std::string filename)
  {
    const std::string tmp (filename + ".tmp");
    std::ofstream * o (new std::ofstream (
      tmp.c_str(), std::ios_base::out|std::ios_base::binary));

    if (!o->good())
      Log::add_err_va (_("Unable to save \"%s\" %s"), filename.c_str(), "");
    ostream_to_filename[o] = filename;
    return o;
  }

  void finalize_ostream (std::ofstream * o)
  {
    g_assert (ostream_to_filename.count(o));
    const std::string filename (ostream_to_filename[o]);
    ostream_to_filename.erase (o);

    o->flush ();
    const bool ok = !o->fail();
    const int my_errno = errno;
    delete o;

    const std::string tmpfile (filename + ".tmp");
    if (ok) {
#if defined(G_OS_WIN32)
      ::remove (filename.c_str());
#endif
      int ret = 0;
      if ((ret = rename (tmpfile.c_str(), filename.c_str())))
      {
        std::cerr << LINE_ID << " ERROR renaming from [" << tmpfile << "] to [" << filename << "]: " << g_strerror(errno) <<" : "<<ret<< '\n';
      } else
      {
        if ((ret = chmod (filename.c_str(), 0600)))
          std::cerr << LINE_ID << " ERROR chmodding [" << filename << "]: " << g_strerror(errno) << " : "<<ret<<'\n';
      }
//      std::cerr<<"dbg "<<ret<<"\n";
    } else {
      Log::add_err_va (_("Unable to save \"%s\" %s"), filename.c_str(), file::pan_strerror(my_errno));
    }
  }
}

std::ostream*
DataIO :: write_tasks ()
{
  return write_file (get_tasks_filename ());
}

std::ostream*
DataIO :: write_server_properties ()
{
  return write_file (get_server_filename());
}

std::ostream*
DataIO :: write_group_xovers ()
{
  return write_file (get_group_xovers_filename ());
}

std::ostream*
DataIO :: write_group_descriptions ()
{
  return write_file (get_group_descriptions_filename ());
}

std::ostream*
DataIO :: write_group_permissions ()
{
  return write_file (get_group_permissions_filename ());
}

std::ostream*
DataIO :: write_group_headers (const Quark& group)
{
  return write_file (get_group_headers_filename (group));
}

std::ostream*
DataIO :: write_download_stats ()
{
   return write_file (get_download_stats_filename ());
}

std::ostream*
DataIO :: write_file (const StringView& filename)
{
  return get_ostream (filename);
}

void
DataIO :: write_done (std::ostream* out)
{
  finalize_ostream (dynamic_cast<std::ofstream*>(out));
}

}
