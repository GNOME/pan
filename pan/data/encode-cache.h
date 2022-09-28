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

#ifndef _EncodeCache_h_
#define _EncodeCache_h_

#include <map>
#include <vector>
#include <glib.h> // for guint64
#include <pan/general/string-view.h>
#include <pan/general/quark.h>

namespace pan
{
  class Article;
  class StringView;

  /**
   * A disk cache for binary attachments to be yenc-encoded
   *
   * It has a lock/unlock mechanism to allow the cache to grow
   * past its limit briefly to allow large multipart articles' pieces
   * to all be held at once (for encoding).
   *
   * FIXME: This should probably be an interface class implemented in
   * data-impl in the same way profiles was.
   *
   * @ingroup data
   */
  class EncodeCache
  {
    public:

      EncodeCache (const StringView& path, size_t max_megs=10);
      ~EncodeCache ();

      FILE* get_fp_from_mid(const Quark& mid);
      void get_filename (char* buf, const Quark& mid) const;
      bool update_file (std::string& data, const Quark& where);

      typedef std::vector<Quark> mid_sequence_t;

      bool contains (const Quark& message_id) const;
      void add (const Quark& message_id);
      void finalize (std::string message_id);
      void get_data(std::string& data, const Quark& where);
      void reserve (const mid_sequence_t& mids);
      void release (const mid_sequence_t& mids);
      void resize ();
      void clear ();

      typedef std::vector<std::string> strings_t;
      strings_t get_filenames (const mid_sequence_t&);

    public:

      /** Interface class for objects that listen to an EncodeCache's events.  */
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
        FILE * _fp;
        MsgInfo(): _size(0), _date(0), _fp(nullptr) {}
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
  };
}


#endif // __EncodeCache_h__
