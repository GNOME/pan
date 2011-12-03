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

#ifndef _ArticleCache_h_
#define _ArticleCache_h_

#include <map>
#include <vector>
extern "C" {
  #include <glib.h> // for guint64
}
#include <pan/general/string-view.h>
#include <pan/general/quark.h>

extern "C"
{
  typedef struct _GMimeMessage GMimeMessage;
  typedef struct _GMimeStream GMimeStream;
}

namespace pan
{
  class Article;
  class StringView;

  /**
   * A disk cache for article bodies.
   *
   * This allows a cache to be set to a certain maximum size, where
   * the oldest articles will be aged out when the cache is full.
   *
   * It also has a lock/unlock mechanism to allow the cache to grow
   * past its limit briefly to allow large multipart articles' pieces
   * to all be held at once (for decoding).
   *
   * FIXME: This should probably be an interface class implemented in
   * data-impl in the same way profiles was.
   *
   * @ingroup data
   */
  class ArticleCache
  {
    public:

      ArticleCache (const StringView& path, size_t max_megs=10);
      ~ArticleCache ();

      typedef std::vector<Quark> mid_sequence_t;

      bool contains (const Quark& message_id) const;
      bool add (const Quark& message_id, const StringView& article);
      void reserve (const mid_sequence_t& mids);
      void release (const mid_sequence_t& mids);
      void resize ();
      void clear ();

      GMimeMessage* get_message (const mid_sequence_t&) const;

      typedef std::vector<std::string> strings_t;
      strings_t get_filenames (const mid_sequence_t&);

    public:

      /** Interface class for objects that listen to an ArticleCache's events.  */
      struct Listener {
        virtual ~Listener () {}
        virtual void on_cache_added (const Quark& mid) = 0;
        virtual void on_cache_removed (const quarks_t& mid) = 0;
      };
      void add_listener (Listener * l) { _listeners.insert(l); }
      void remove_listener (Listener * l) { _listeners.erase(l); }

    private:

      std::map<Quark,int> _locks;

      struct MsgInfo {
        Quark _message_id;
        size_t _size;
        time_t _date;
        MsgInfo(): _size(0), _date(0) {}
      };

      typedef std::map<Quark,MsgInfo> mid_to_info_t;
      mid_to_info_t _mid_to_info;

      struct MsgInfoCompare {
        bool operator()(const MsgInfo& a, const MsgInfo& b) const {
          if (a._date != b._date)
            return a._date < b._date;
          return a._message_id < b._message_id;
        }
      };

      std::string _path;
      const size_t _max_megs;
      guint64 _current_bytes;

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

      void fire_added (const Quark& mid);
      void fire_removed (const quarks_t& mid);

      void resize (guint64 max_bytes);

      char* get_filename (char* buf, int buflen, const Quark& mid) const;
      GMimeStream* get_message_file_stream (const Quark& mid) const;
      GMimeStream* get_message_mem_stream (const Quark& mid) const;
  };
}


#endif // __ArticleCache_h__
