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

#ifndef _ArticleCache_h_
#define _ArticleCache_h_

#include <config.h>
#include <glib.h> // for guint64
#include <map>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <vector>

#include <pan/usenet-utils/gpg.h>

extern "C"
{
  typedef struct _GMimeMessage GMimeMessage;
  typedef struct _GMimeStream GMimeStream;
}

namespace pan {

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
    enum CacheResponse_types
    {
      CACHE_IO_ERR,
      CACHE_DISK_FULL,
      CACHE_OK
    };

    struct CacheResponse
    {
        CacheResponse_types type;
        std::string err; // perhaps use gerror here??
    };

    ArticleCache(StringView const &path,
                 StringView const &extension,
                 size_t max_megs = 10);
    ~ArticleCache();

    typedef std::vector<Quark> mid_sequence_t;

    bool contains(Quark const &message_id) const;
    CacheResponse add(const Quark &message_id,
                      const StringView &article,
                      const bool virtual_file = false);
    void reserve(const mid_sequence_t &mids);
    void release(const mid_sequence_t &mids);
    void resize();
    void clear();
#ifdef HAVE_GMIME_CRYPTO
      GMimeMessage* get_message (const mid_sequence_t&, GPGDecErr&) const;
#else
      GMimeMessage* get_message (const mid_sequence_t&) const;
#endif
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

    public:
      void set_max_megs (size_t value) { _max_megs = value; }
      void set_msg_extension (const std::string& s) { msg_extension = s; }
      const std::string& get_msg_extension () const { return msg_extension; }

    private:

      std::map<Quark,int> _locks;

      std::string msg_extension;

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
      size_t _max_megs; // changeable via prefs
      guint64 _current_bytes;

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

      void fire_added (const Quark& mid);
      void fire_removed (const quarks_t& mid);

      void resize (guint64 max_bytes);

      char* get_filename (char* buf, int buflen, const Quark& mid) const;
      GMimeStream* get_message_file_stream (const Quark& mid) const;
      GMimeStream* get_message_mem_stream (const Quark& mid) const;

      int filename_to_message_id (char * buf, int len, const char * basename);
      char* message_id_to_filename (char * buf, int len, const StringView& mid) const;
};
} // namespace pan

#endif // __ArticleCache_h__
