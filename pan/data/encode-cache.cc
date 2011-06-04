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
namespace
{
   char*
   message_id_to_filename (char * buf, const Quark& mid)
   {
      int partno();
      g_snprintf (buf, sizeof(buf), "%s.%d", mid.c_str(), partno);
      std::cerr<<"ec mid to fn "<<buf<<"\n";
      return buf;
   }

   int
   filename_to_message_id (char * buf, int len, const char * basename)
   {
      const char * in;
      char * out;
      char * pch;
      char tmp_basename[PATH_MAX];

      // sanity clause
      pan_return_val_if_fail (basename && *basename, 0);
      pan_return_val_if_fail (buf!=NULL, 0);
      pan_return_val_if_fail (len>0, 0);

      // remove the trailing ".msg"
      g_strlcpy (tmp_basename, basename, sizeof(tmp_basename));
      if ((pch = g_strrstr (tmp_basename, ".msg")))
         *pch = '\0';
      g_strstrip (tmp_basename);

      // transform
      out = buf;
      *out++ = '<';
      for (in=tmp_basename; *in; ++in) {
         if (in[0]!='%' || !g_ascii_isxdigit(in[1]) || !g_ascii_isxdigit(in[2]))
            *out++ = *in;
         else {
            char buf[3];
            buf[0] = *++in;
            buf[1] = *++in;
            buf[2] = '\0';
            *out++ = (char) strtoul (buf, NULL, 16);
         }
      }
      *out++ = '>';
      *out = '\0';

      return out - buf;
      std::cerr<<"ec fn to mid "<<(out-buf)<<"\n";
   }
};

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


     ///TODO import articles, see nzb.ccc
//      char filename[PATH_MAX];
//      const char * fname;
//      while ((fname = g_dir_read_name (dir)))
//      {
//         struct stat stat_p;
//         g_snprintf (filename, sizeof(filename), "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, fname);
//         if (!stat (filename, &stat_p))
//         {
//            char str[2048];
//            const int len (filename_to_message_id (str, sizeof(str), fname));
//            if (len != 0)
//            {
//               MsgInfo info;
//               info._message_id = StringView (str, len);
//               info._size = stat_p.st_size;
//               info._date = stat_p.st_mtime;
////               _current_bytes += info._size;
//               _mid_to_info.insert (mid_to_info_t::value_type (info._message_id, info));
//            }
//         }
//      }
//      g_dir_close (dir);
//      debug ("loaded " << _mid_to_info.size() << " articles into cache from " << _path);
   }
}

EncodeCache :: ~EncodeCache ()
{
}

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
   const char* base = g_path_get_basename(mid.c_str());
   g_snprintf (buf, PATH_MAX, "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, base);
   g_free((gpointer)base);
   std::cerr<<"ec get_filename "<<buf<<"\n";
}

///TODO give function a reference to vector!!
FILE*
EncodeCache :: get_fp_from_mid(const Quark& mid)
{
  std::cerr<<"ec get_fp from mid "<<mid.c_str()<<"\n";
  std::cerr<<"mid to info : "<<_mid_to_info[mid]._message_id<<std::endl;
  return _mid_to_info[mid]._fp;
}

FILE*
EncodeCache :: add (const Quark& message_id)
{

  std::cerr<<"ec add "<<message_id<<"\n";

  pan_return_val_if_fail (!message_id.empty(), false);

  FILE * fp = 0;
  char filename[PATH_MAX];
  struct stat sb;
  get_filename (filename, message_id);
  fp = fopen (filename, "wb+");

  if (!fp)
  {
    Log::add_err_va (_("Unable to save \"%s\" %s"),
                     filename, file::pan_strerror(errno));
  }
  else
  {
    std::cerr<<"building msginfo\n";
    MsgInfo info;
    info._fp = fp;
    info._message_id = message_id;
    stat (message_id, &sb);
    info._size = sb.st_size;
    info._date = time(0);
    _current_bytes += info._size;
    _mid_to_info.insert (mid_to_info_t::value_type (info._message_id, info));
    fire_added (message_id);
    resize ();
  }
  return fp;
}

/***
****
***/

void
EncodeCache :: reserve (const mid_sequence_t& mids)
{
  std::cerr<<"ec reserve\n";
  foreach_const (mid_sequence_t, mids, it)
    ++_locks[*it];
}

void
EncodeCache :: release (const mid_sequence_t& mids)
{
  std::cerr<<"ec release\n";
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
  char buf[4096];
  get_filename(buf, where);
  std::ifstream in(buf, std::ifstream::in);
  std::stringstream out;
  if (in.bad())
    std::cerr<<"error getting file "<<where.c_str()<<std::endl;
  while (in.good())
    out << (char) in.get();

  data = out.str();
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
        if (_mid_to_info[mid]._fp) fclose(_mid_to_info[mid]._fp);
        _mid_to_info.erase (mid);
      }
    }
  }

  std::cerr<< "cache expired " << removed.size() << " articles, "
         "has " << _mid_to_info.size() << " active "
         "and " << _locks.size() << " locked.\n";

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
