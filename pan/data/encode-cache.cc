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
#include <sstream>

extern "C"
{
  #include <errno.h>
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <unistd.h>
  #include <dirent.h>

  #include <glib.h>
  #include <glib/gi18n.h>
  #include <gmime/gmime.h>
}

#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>
#include "article.h"
#include "encode-cache.h"

using namespace pan;

/*****
******
*****/

EncodeCache :: EncodeCache (const StringView& path, size_t max_megs):
   _path (path.str, path.len),
   _max_megs (max_megs),
   _current_bytes (0ul)
{
   GError * err = NULL;
   GDir * dir = g_dir_open (_path.c_str(), 0, &err);
   if (err != NULL)
   {
      Log::add_err_va (_("Error opening directory: \"%s\": %s"), _path.c_str(), err->message);
      g_clear_error (&err);
   }
   else
   {
      char filename[PATH_MAX];
      const char * fname;
      while ((fname = g_dir_read_name (dir)))
      {
        g_snprintf (filename, sizeof(filename), "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, fname);
        // delete ALL but the gmime_messages
        if (!strstr(filename, "_msg_")) unlink(filename);
      }
      g_dir_close (dir);
   }
}

EncodeCache :: ~EncodeCache ()
{}

/*****
******
*****/

void
EncodeCache :: fire_added (const Quark& mid)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_cache_added (mid);
}

void
EncodeCache :: fire_removed (const quarks_t& mids)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_cache_removed (mids);
}

/*****
******
*****/

bool
EncodeCache :: contains (const Quark& mid) const
{
  return _mid_to_info.find (mid) != _mid_to_info.end();
}

void
EncodeCache :: get_filename (char* buf, const Quark& mid) const
{
   char* base = g_path_get_basename(mid.c_str());
   g_snprintf (buf, PATH_MAX, "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, base);
   g_free(base);
}

FILE*
EncodeCache :: get_fp_from_mid(const Quark& mid)
{
  char buf[PATH_MAX];
  get_filename(buf, mid);
  return fopen(buf,"wb+");
}

void
EncodeCache :: add (const Quark& message_id)
{

  MsgInfo info;
  info._message_id = message_id;
  info._size = 0;
  info._date = time(0);
//  info._fp = get_fp_from_mid(message_id);
  _mid_to_info.insert (mid_to_info_t::value_type (info._message_id, info));
}

/***
****
***/

void EncodeCache :: finalize (std::string& message_id)
{
  struct stat sb;
  stat (message_id.c_str(), &sb);
  _mid_to_info[message_id]._size = sb.st_size;
  fire_added (message_id);
  _current_bytes += sb.st_size;
  resize();
}

void
EncodeCache :: reserve (const mid_sequence_t& mids)
{
  foreach_const (mid_sequence_t, mids, it)
    ++_locks[*it];
}

void
EncodeCache :: release (const mid_sequence_t& mids)
{
  foreach_const (mid_sequence_t, mids, it)
    if (!--_locks[*it])
      _locks.erase (*it);
}

/***
****
***/

void
EncodeCache :: resize ()
{
  // let's shrink it to 80% of the maximum size
  const double buffer_zone (0.8);
  guint64 max_bytes (_max_megs * 1024 * 1024);
  max_bytes = (guint64) ((double)max_bytes * buffer_zone);
  resize (max_bytes);
}

void
EncodeCache :: clear ()
{
  resize (0);
}

void
EncodeCache :: get_data(std::string& data, const Quark& where)
{
  char buf[4096], buf2[4096];
  get_filename(buf, where);
  std::ifstream in(buf, std::ios::in);
  std::stringstream out;

  while (in.getline(buf,sizeof(buf)))
    out << buf << "\n";

  in.close();
  data = out.str();
}

bool
EncodeCache :: update_file (std::string& data, const Quark& where)
{
  FILE * fp = get_fp_from_mid(where);
  if (!fp) return false;
  fwrite(data.c_str(), strlen(data.c_str()), sizeof(char), fp);
  fclose(fp);
  return true;
}

void
EncodeCache :: resize (guint64 max_bytes)
{
  quarks_t removed;
  if (_current_bytes > max_bytes)
  {
    // sort from oldest to youngest
    typedef std::set<MsgInfo, MsgInfoCompare> sorted_info_t;
    sorted_info_t si;
    for (mid_to_info_t::const_iterator it=_mid_to_info.begin(),
         end=_mid_to_info.end(); it!=end; ++it)
      si.insert (it->second);

    // start blowing away files
    for (sorted_info_t::const_iterator it=si.begin(), end=si.end();
         _current_bytes>max_bytes && it!=end; ++it) {
      const Quark& mid (it->_message_id);
      if (_locks.find(mid) == _locks.end()) {
        char buf[PATH_MAX];
        get_filename (buf, mid);
        unlink (buf);
        _current_bytes -= it->_size;
        removed.insert (mid);
//        if (it->_fp) fclose(it->_fp);
        _mid_to_info.erase (mid);
      }
    }
  }

  debug("cache expired " << removed.size() << " articles, "
        "has " << _mid_to_info.size() << " active "
        "and " << _locks.size() << " locked.\n");

  if (!removed.empty())
    fire_removed (removed);
}

EncodeCache :: strings_t
EncodeCache :: get_filenames (const mid_sequence_t& mids)
{
  strings_t ret;
  char filename[PATH_MAX];
  foreach_const (mid_sequence_t, mids, it) {
    get_filename (filename, *it);
    ret.push_back (filename);
  }
  return ret;
}
