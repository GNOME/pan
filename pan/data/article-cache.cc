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
#include <pan/general/foreach.h>
#include <pan/general/messages.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>
#include "article.h"
#include "article-cache.h"

using namespace pan;

/*****
******
*****/

namespace
{
   /** 
    * Some characters in message-ids don't work well in filenames,
    * so we transform them to a safer name.
    */
   char*
   message_id_to_filename (char * buf, int len, const StringView& mid)
   {
      // sanity clause
      pan_return_val_if_fail (!mid.empty(), 0);
      pan_return_val_if_fail (buf!=0, NULL);
      pan_return_val_if_fail (len>0, NULL);
                                                                                                                     
      // some characters in message-ids are illegal on older Windows boxes,
      // so we transform those illegal characters using URL encoding
      char * out = buf;
      for (const char *in=mid.begin(), *end=mid.end(); in!=end; ++in) {
         switch (*in) {
            case '%': /* this is the escape character */
            case '"': case '*': case '/': case ':': case '?': case '|':
            case '\\': /* these are illegal on vfat, fat32 */
               g_snprintf (out, len-(out-buf), "%%%02x", (int)*in);
               out += 3;
               break;
            case '<': case '>': /* these are illegal too, but rather than encoding
                                   them, follow the convention of omitting them */
               break;
            default:
               *out++ = *in;
         }
      }

      g_snprintf (out, len-(out-buf), ".msg");
      return buf;
   }

   /**
    * Message-IDs are transformed via message_id_to_filename()
    * to play nicely with some filesystems, so to extract the Message-ID
    * from a filename we need to reverse the transform.
    *
    * @return string length, or 0 on failure
    */
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
   }
};

/*****
******
*****/

ArticleCache :: ArticleCache (const StringView& path, size_t max_megs):
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
         struct stat stat_p;
         g_snprintf (filename, sizeof(filename), "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, fname);
         if (!stat (filename, &stat_p))
         {
            char str[2048];
            const int len (filename_to_message_id (str, sizeof(str), fname));
            if (len != 0)
            {
               MsgInfo info;
               info._message_id = StringView (str, len);
               info._size = stat_p.st_size;
               info._date = stat_p.st_mtime;
               _current_bytes += info._size;
               _mid_to_info.insert (mid_to_info_t::value_type (info._message_id, info));
            }
         }
      }
      g_dir_close (dir);
      debug ("loaded " << _mid_to_info.size() << " articles into cache from " << _path);
   }
}

ArticleCache :: ~ArticleCache ()
{
}

/*****
******
*****/

void
ArticleCache :: fire_added (const Quark& mid)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_cache_added (mid);
}

void
ArticleCache :: fire_removed (const quarks_t& mids)
{
  for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_cache_removed (mids);
}

/*****
******
*****/
                                                                                                                                                                 
bool
ArticleCache :: contains (const Quark& mid) const
{
  return _mid_to_info.find (mid) != _mid_to_info.end();
}

char*
ArticleCache :: get_filename (char * buf, int buflen, const Quark& mid) const
{
   char basename[PATH_MAX];
   *buf = '\0';
   message_id_to_filename (basename, sizeof(basename), mid.to_view());
   g_snprintf (buf, buflen, "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, basename);
   return buf && *buf ? buf : 0;
};

bool
ArticleCache :: add (const Quark& message_id, const StringView& article)
{
  debug ("adding " << message_id << ", which is " << article.len << " bytes long");

  pan_return_val_if_fail (!message_id.empty(), false);
  pan_return_val_if_fail (!article.empty(), false);

  FILE * fp = 0;
  char filename[PATH_MAX];
  if (get_filename (filename, sizeof(filename), message_id))
    fp = fopen (filename, "wb+");

  if (!fp)
  {
      Log::add_err_va (_("Unable to save \"%s\" %s"),
                       filename, file::pan_strerror(errno));
      return false;
  }
  else
  {
    const size_t bytes_written (fwrite (article.str, sizeof(char), article.len, fp));
    fclose (fp);

    if (bytes_written < article.len)
    {
      Log::add_err_va (_("Unable to save \"%s\" %s"),
                       filename, file::pan_strerror(errno));
      return false;
    }
    else
    {
      MsgInfo info;
      info._message_id = message_id;
      info._size = article.len;
      info._date = time(0);
      _mid_to_info.insert (mid_to_info_t::value_type (info._message_id, info));
      fire_added (message_id);

      _current_bytes += info._size;
      resize ();
    }
  }

  return true;
}

/***
****
***/

void
ArticleCache :: reserve (const mid_sequence_t& mids)
{
  foreach_const (mid_sequence_t, mids, it)
    ++_locks[*it];
}

void
ArticleCache :: release (const mid_sequence_t& mids)
{
  foreach_const (mid_sequence_t, mids, it)
    if (!--_locks[*it])
      _locks.erase (*it);
}

/***
****
***/

void
ArticleCache :: resize ()
{
  // let's shrink it to 80% of the maximum size
  const double buffer_zone (0.8);
  guint64 max_bytes (_max_megs * 1024 * 1024);
  max_bytes = (guint64) ((double)max_bytes * buffer_zone);
  resize (max_bytes);
}

void
ArticleCache :: clear ()
{
  resize (0);
}

void
ArticleCache :: resize (guint64 max_bytes)
{
  quarks_t removed;
  if (_current_bytes > max_bytes)
  {
    // sort from oldest to youngest
    typedef std::set<MsgInfo, MsgInfoCompare> sorted_info_t;
    sorted_info_t si;
    for (mid_to_info_t::const_iterator it=_mid_to_info.begin(), end=_mid_to_info.end(); it!=end; ++it)
      si.insert (it->second);

    // start blowing away files
    for (sorted_info_t::const_iterator it=si.begin(), end=si.end(); _current_bytes>max_bytes && it!=end; ++it) {
      const Quark& mid (it->_message_id);
      if (_locks.find(mid) == _locks.end()) {
        char buf[PATH_MAX];
        get_filename (buf, sizeof(buf), mid);
        unlink (buf);
        _current_bytes -= it->_size;
        removed.insert (mid);
        debug ("removing [" << mid << "] as we resize the queue");
        _mid_to_info.erase (mid);
      }
    }
  }

  debug ("cache expired " << removed.size() << " articles, "
         "has " << _mid_to_info.size() << " active "
         "and " << _locks.size() << " locked.");

  if (!removed.empty())
    fire_removed (removed);
}

/****
*****
*****  Getting Messages
*****
****/

/*private*/ GMimeStream*
ArticleCache :: get_message_file_stream (const Quark& mid) const
{
   GMimeStream * retval = NULL;

   /* open the file */
   char filename[PATH_MAX];
   if (get_filename (filename, sizeof(filename), mid))
   {
      errno = 0;
      FILE * fp = fopen (filename, "rb");
      if (!fp)
         Log::add_err_va (_("Error opening file \"%s\" %s"), filename, file::pan_strerror(errno));
      else {
         GMimeStream * file_stream = g_mime_stream_file_new (fp);
         retval = g_mime_stream_buffer_new (file_stream, GMIME_STREAM_BUFFER_BLOCK_READ);
         g_object_unref (file_stream);
      }
   }

   debug ("file stream for " << mid << ": " << retval);
   return retval;
}

/*private*/ GMimeStream*
ArticleCache :: get_message_mem_stream (const Quark& mid) const
{
   debug ("mem stream got quark " << mid);
   GMimeStream * retval (0);

   char filename[PATH_MAX];
   if (get_filename (filename, sizeof(filename), mid))
   {
      debug ("mem stream loading filename " << filename);
      gsize len (0);
      char * buf (0);
      GError * err (0);

      if (g_file_get_contents (filename, &buf, &len, &err)) {
         debug ("got the contents, calling mem_new_with_buffer");
         retval = g_mime_stream_mem_new_with_buffer (buf, len);
         g_free (buf);
      } else {
         Log::add_err_va (_("Error reading file \"%s\": %s"), filename, err->message);
         g_clear_error (&err);
      }
   }

   debug ("mem stream for " << mid << ": " << retval);
   return retval;
}

GMimeMessage*
ArticleCache :: get_message (const mid_sequence_t& mids)
{
   debug ("trying to get a message with " << mids.size() << " parts");
   GMimeMessage * retval = NULL;

   // load the streams
   typedef std::vector<GMimeStream*> streams_t;
   streams_t streams;
   const bool in_memory (mids.size() <= 2u);
   foreach_const (mid_sequence_t, mids, it) {
      const Quark mid (*it);
      GMimeStream * stream (0);
      if (this->contains (*it))
        stream = in_memory
          ? get_message_mem_stream (mid)
          : get_message_file_stream (mid);
      if (stream)
        streams.push_back (stream);
   }

   // build the message
   if (!streams.empty())
     retval = mime :: construct_message (&streams.front(), streams.size());

   // cleanup
   foreach (streams_t, streams, it)
     g_object_unref (*it);

   debug ("returning " << retval);
   return retval;
}

ArticleCache :: strings_t
ArticleCache :: get_filenames (const mid_sequence_t& mids)
{
  strings_t ret;
  char filename[PATH_MAX];
  foreach_const (mid_sequence_t, mids, it)
    if (get_filename (filename, sizeof(filename), *it))
      ret.push_back (filename);
  return ret;
}
