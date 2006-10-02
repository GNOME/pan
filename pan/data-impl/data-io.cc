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
  #include <glib.h>
  #include <glib/gi18n.h>
}
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

  std::string get_newsrc_filename (const Quark& server)
  {
    std::string tmp ("newsrc-");
    tmp +=  server;
    return get_pan_home_file (tmp.c_str());
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

quarks_t
DataIO :: get_newsrc_servers () const
{
  quarks_t servers;
  const std::string pan_home (file::get_pan_home ());
  GDir * dir (g_dir_open (pan_home.c_str(), 0, NULL));
  const char * fname;
  while (dir && ((fname = g_dir_read_name (dir))))
    if (!strncmp (fname, "newsrc-", 7))
      servers.insert (fname+7);
  g_dir_close (dir);
  return servers;
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

void
DataIO :: erase_newsrc (const Quark& server)
{
  const std::string filename (get_newsrc_filename (server));
  std::remove (filename.c_str());
}

/****
*****
****/

LineReader*
DataIO :: read_tasks () const
{
  const std::string filename (get_tasks_filename ());
  FileLineReader * in (0);
  if (file :: file_exists (filename.c_str()))
    in = new FileLineReader (filename.c_str());
  return in;
}

LineReader*
DataIO :: read_group_descriptions () const
{
   const std::string filename (get_group_descriptions_filename ());
   return new FileLineReader (filename.c_str());
}

LineReader*
DataIO :: read_group_permissions () const
{
   const std::string filename (get_group_permissions_filename ());
   return new FileLineReader (filename.c_str());
}

LineReader*
DataIO :: read_group_xovers () const
{
   const std::string filename (get_group_xovers_filename ());
   return new FileLineReader (filename.c_str());
}

LineReader*
DataIO :: read_group_headers (const Quark& group) const
{
  const std::string filename (get_group_headers_filename (group));
  FileLineReader * in (0);
  if (file :: file_exists (filename.c_str()))
    in = new FileLineReader (filename.c_str());
  return in;
}

LineReader*
DataIO :: read_newsrc (const Quark& server) const
{
  const std::string filename (get_newsrc_filename (server));
  return new FileLineReader (filename.c_str());
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
      ::remove (filename.c_str());
      ::rename (tmpfile.c_str(), filename.c_str());
    } else {
      Log::add_err_va (_("Unable to save \"%s\" %s"), filename.c_str(), file::pan_strerror(my_errno));
      ::remove (tmpfile.c_str());
    }
  }
}

std::ostream*
DataIO :: write_tasks ()
{
  return get_ostream (get_tasks_filename ());
}

std::ostream*
DataIO :: write_server_properties ()
{
  return get_ostream (get_server_filename());
}

std::ostream*
DataIO :: write_group_xovers ()
{
  return get_ostream (get_group_xovers_filename ());
}

std::ostream*
DataIO :: write_group_descriptions ()
{
  return get_ostream (get_group_descriptions_filename ());
}

std::ostream*
DataIO :: write_group_permissions ()
{
  return get_ostream (get_group_permissions_filename ());
}

std::ostream*
DataIO :: write_group_headers (const Quark& group)
{
  return get_ostream (get_group_headers_filename (group));
}

std::ostream*
DataIO :: write_newsrc (const Quark& server)
{
  return get_ostream (get_newsrc_filename (server));
}

void
DataIO :: write_done (std::ostream* out)
{
  finalize_ostream (dynamic_cast<std::ofstream*>(out));
}
