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
#include <cerrno>
#include <cstdio>
#include <map>
#include <iostream>
#include <fstream>
extern "C" {
  #include <sys/types.h> // for chmod
  #include <sys/stat.h> // for chmod
  #include <unistd.h>
  #include <glib.h>
  #include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/line-reader.h>
#include <pan/general/log.h>
#include "data-io.h"

using namespace pan;

namespace
{
  std::string get_pan_home_file (const char * fname)
  {
    const std::string home (file::get_pan_home());
    char * filename (g_build_filename (home.c_str(), fname, NULL));
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

  std::string get_group_headers_filename (const Quark& group)
  {
    const std::string home (file::get_pan_home());
    char * filename (g_build_filename (home.c_str(), "groups", group.c_str(), NULL));
    char * dirname (g_path_get_dirname (filename));
    file :: ensure_dir_exists (dirname);
    std::string retval (filename);
    g_free (dirname);
    g_free (filename);
    return retval;
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
    char * path (g_build_filename (g_get_home_dir(), "News", "Score", NULL));
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
  return file::file_exists(filename.c_str()) ? read_file(filename) : 0;
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
DataIO :: read_group_xovers () const
{
  return read_file (get_group_xovers_filename ());
}

LineReader*
DataIO :: read_group_headers (const Quark& group) const
{
  const std::string filename (get_group_headers_filename (group));
  return file::file_exists(filename.c_str()) ? read_file(filename) : 0;
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
//      ::remove (filename.c_str()); not needed
      if (rename (tmpfile.c_str(), filename.c_str()))
        std::cerr << LINE_ID << " ERROR renaming from [" << tmpfile << "] to [" << filename << "]: " << g_strerror(errno) << '\n';
      if (chmod (filename.c_str(), 0600))
        std::cerr << LINE_ID << " ERROR chmodding [" << filename << "]: " << g_strerror(errno) << '\n';
    } else {
      Log::add_err_va (_("Unable to save \"%s\" %s"), filename.c_str(), file::pan_strerror(my_errno));
      ::remove (tmpfile.c_str());
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
DataIO :: write_file (const StringView& filename)
{
  return get_ostream (filename);
}

void
DataIO :: write_done (std::ostream* out)
{
  finalize_ostream (dynamic_cast<std::ofstream*>(out));
}
