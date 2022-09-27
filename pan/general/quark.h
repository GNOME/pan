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

#ifndef _Quark_h_
#define _Quark_h_

#include <stdint.h>
#include <cassert>
#include <ostream>
#include <set>
#include <string>
#include <climits>
#include <string>
#include <unordered_set>
#include <vector>

#include <pan/general/string-view.h>

#ifndef UINT32_MAX
#define UINT32_MAX             (4294967295U)
#endif

namespace pan
{
  /**
   * A two-way association between a string and integral type identifier.
   *
   * Quarks make good keys because comparision operations can be done
   * on the integral type instead of with costly strcmps.
   *
   * In X11 and Gtk+ implementations, the string+integral mapping is
   * permanent.  However pan::Quark frees its copy of the mapped string when
   * the last corresponding pan::Quark is destroyed.  This way Quarks can be
   * used even on large, transient sets of data (such as Message-IDs) without
   * leaking the keys.
   *
   * There is, obviously, a tradeoff involved: hashing strings can be
   * expensive, and the refcounted hashtable of strings has its own
   * memory overhead.  So while strings that are likely to be duplicated
   * or used as keys -- message-ids, author names, and group names
   * spring to mind -- they're less appropriate for temporary, unique data.
   *
   * @ingroup general
   */
  class Quark
  {
    private:

      struct Impl {
        uint32_t refcount;
        uint32_t len;
        char * str;
        Impl (): refcount(0), len(0), str(nullptr) {}
        Impl (const StringView& v): refcount(0), len(v.len), str(const_cast<char*>(v.str)) {}
        StringView to_view () const { return StringView(str,len); }
        //wtf? bool operator() (const Impl& a, const Impl& b) const { return StringView(str,len) == StringView(b.str,b.len); }
        bool operator== (const Impl& b) const {
          return StringView(str,len) == StringView(b.str,b.len);
        }
        bool operator< (const Impl& b) const {
          return StringView(str,len) < StringView(b.str,b.len);
        }
      };

      struct StringViewHash
      {
        static uint16_t get16bits( const char * in )
        {
          return (static_cast<uint16_t>(in[0])<<8) | in[1];
        }

        /**
         * Paul Hsieh's "SuperFastHash" algorithm, from
         * http://www.azillionmonkeys.com/qed/hash.html
         */
        size_t operator()(const Impl& s) const
        {
          const char * data (s.str);
          int len (s.len);

          if (len <= 0 || data == NULL) return 0;

          uint32_t hash = len;

          int rem = len & 3;
          len >>= 2;

          /* Main loop */
          for (;len > 0; len--) {
              hash += get16bits (data);
              uint32_t tmp = (static_cast<uint32_t>(get16bits(data + 2)) << 11) ^ hash;
              hash = (hash << 16) ^ tmp;
              data += 2*sizeof (uint16_t);
              hash += hash >> 11;
          }

          /* Handle end cases */
          switch (rem) {
              case 3:
                hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= static_cast<uint32_t>(data[sizeof (uint16_t)]) << 18;
                hash += hash >> 11;
                break;

              case 2:
                hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;

              case 1:
                hash += *data;
                hash ^= hash << 10;
                hash += hash >> 1;
          }

          /* Force "avalanching" of final 127 bits */
          hash ^= hash << 3;
          hash += hash >> 5;
          hash ^= hash << 4;
          hash += hash >> 17;
          hash ^= hash << 25;
          hash += hash >> 6;

          return hash;
        }
      };


      typedef std::unordered_set<Impl, StringViewHash> lookup_t;

      static lookup_t _lookup;

      static Impl* init (const StringView& s)
      {
        std::pair<lookup_t::iterator,bool> result (_lookup.insert (Impl(s)));
        Impl * impl = const_cast<Impl*>(&*result.first);
        if (result.second)
        {
         // new item -- populate the entry with a new string
          impl->str = new char [s.len + 1];
          memcpy (impl->str, s.str, s.len);
          impl->str[s.len] = '\0';
          impl->len = s.len;
        }
        assert (impl->refcount!=UINT32_MAX);
        ++impl->refcount;
        return impl;
      }

      void unref () {
        if (impl!=nullptr) {
          assert (impl->refcount);
          if (!--impl->refcount) {
            const Impl tmp (*impl);
            _lookup.erase (tmp);
            impl = nullptr;
            delete [] (char*)tmp.str;
          }
        }
      }

    private:
      Impl * impl;

    public:
      Quark (): impl(nullptr) {}
      Quark (const std::string & s): impl (init (StringView (s))) { }
      Quark (const char * s): impl (init (StringView (s))) { }
      Quark (const StringView &  p): impl (init (p)) { }
      Quark (const Quark& q) {
        if (q.impl != nullptr) ++q.impl->refcount;
        impl = q.impl;
      }

      Quark& operator= (const Quark& q) {
        if (q.impl != nullptr) ++q.impl->refcount;
        unref ();
        impl = q.impl;
        return *this;
      }

      ~Quark () { clear(); }
      void clear () { unref(); impl=nullptr; }
      bool empty() const { return impl == nullptr; }
      bool operator== (const char * that) const {
        const char * pch = c_str ();
        if (!pch && !that) return true;
        if (!pch || !that) return false;
        return !strcmp(c_str(), that);
      }
      bool operator!= (const char * that) const { return !(*this == that); }
      bool operator== (const StringView& that) const { return impl ? !that.strcmp(impl->str,impl->len) :  that.empty(); }
      bool operator!= (const StringView& that) const { return impl ?  that.strcmp(impl->str,impl->len) : !that.empty(); }
      bool operator== (const Quark& q) const { return impl == q.impl; }
      bool operator!= (const Quark& q) const { return impl != q.impl; }
      bool operator< (const Quark& q) const { return impl < q.impl; }
      bool operator! () const { return empty(); }
      const StringView to_view () const { return impl ? impl->to_view() : StringView(); }
      const char* c_str () const { return impl ? impl->str : NULL; }
      std::string to_string () const { return std::string (impl ? impl->str : ""); }
      operator const char* () const { return c_str(); }

      /** Number of unique strings being mapped.
          Included for debugging and regression tests... */
      static unsigned long size () { return _lookup.size(); }
      static void dump (std::ostream&);
  };

  std::ostream& operator<< (std::ostream& os, const Quark& s);

  typedef std::set<Quark> quarks_t;

  typedef std::vector<Quark> quarks_v;

  /**
   * StrictWeakOrdering which sorts Quarks alphabetically.
   * This should be used sparingly, as it defeats Quark's main speed advantage.
   */
  struct AlphabeticalQuarkOrdering {
    bool operator() (const Quark& a, const Quark& b) const {
      if (a.empty() && b.empty()) return false;
      if (a.empty()) return true;
      if (b.empty()) return false;
      return ::strcmp (a.c_str(), b.c_str()) < 0;
    }
    bool operator() (const Quark* a, const Quark* b) const {
      return (*this) (*a, *b);
    }
  };
}

#endif
