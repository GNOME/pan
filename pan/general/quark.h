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

#ifndef _Quark_h_
#define _Quark_h_

#include <stdint.h>
#include <cassert>
#include <ostream>
#include <set>
#include <string>
#include <climits>

#if defined(HAVE_EXT_HASH_MAP)
# include <ext/hash_map>
#else
# include <map>
#endif
#include <pan/general/string-view.h>

#define get16bits(d) (*((const uint16_t *) (d)))

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

      struct StringViewHash
      {
        /**
         * Paul Hsieh's "SuperFastHash" algorithm, from
         * http://www.azillionmonkeys.com/qed/hash.html
         */
        size_t operator()(const StringView& s) const
        {
          const char * data (s.str);
          int len (s.len);
          uint32_t tmp, hash=s.len;
          if (len <= 0 || data == NULL) return 0;

          int rem = len & 3;
          len >>= 2;

          /* Main loop */
          for (;len > 0; len--) {
              hash  += get16bits (data);
              tmp    = (get16bits (data+2) << 11) ^ hash;
              hash   = (hash << 16) ^ tmp;
              data  += 2*sizeof (uint16_t);
              hash  += hash >> 11;
          }

          /* Handle end cases */
          switch (rem) {
              case 3: hash += get16bits (data);
                      hash ^= hash << 16;
                      hash ^= data[sizeof (uint16_t)] << 18;
                      hash += hash >> 11;
                      break;
              case 2: hash += get16bits (data);
                      hash ^= hash << 11;
                      hash += hash >> 17;
                      break;
              case 1: hash += *data;
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

      struct Impl {
        unsigned long refcount;
        StringView * view;
        Impl(): refcount(0), view(0) {}
      };

#if defined(HAVE_EXT_HASH_MAP)
      typedef __gnu_cxx::hash_map < StringView, Impl, StringViewHash > key_to_impl_t;
#else
      typedef std::map<StringView, Impl> key_to_impl_t;
#endif
      static key_to_impl_t _lookup;

      static Impl* init (const StringView& s) {
        Impl impl;
        std::pair<key_to_impl_t::iterator,bool> result (_lookup.insert (std::pair<StringView,Impl>(s,impl)));
        key_to_impl_t::iterator& it (result.first);
        if (result.second)
        {
         // new item -- populate the entry with a new string
          char * pch = (char*) malloc (s.len + 1);
          memcpy (pch, s.str, s.len);
          pch[s.len] = '\0';
          StringView * view (const_cast<StringView*>(&it->first));

          view->assign (pch, s.len); // key has same value, but now we own it.
          it->second.view = view;
        }
        assert (it->second.refcount!=ULONG_MAX);
        ++it->second.refcount;
        return &it->second;
      }

      void unref () {
        if (impl!=0) {
          assert (impl->refcount);
          if (!--impl->refcount) {
            StringView view (*impl->view);
            _lookup.erase (view);
            impl = 0;
            free ((char*)view.str);
          }
        }
      }

    private:
      Impl * impl;

    public:
      Quark (): impl(0) {}
      Quark (const std::string & s): impl (init (StringView (s))) { }
      Quark (const char * s): impl (init (StringView (s))) { }
      Quark (const StringView &  p): impl (init (p)) { }
      Quark (const Quark& q) {
        if (q.impl != 0) ++q.impl->refcount;
        impl = q.impl;
      }

      Quark& operator= (const Quark& q) {
        if (q.impl != 0) ++q.impl->refcount;
        unref ();
        impl = q.impl;
        return *this;
      }

      ~Quark () { clear(); }
      void clear () { unref(); impl=0; }
      bool empty() const { return impl == 0; }
      bool operator== (const char * that) const { return !strcmp(c_str(), that); }
      bool operator!= (const char * that) const { return !(*this == that); }
      bool operator== (const StringView& that) const { return impl ? (that == *impl->view) : that.empty(); }
      bool operator!= (const StringView& that) const { return !(*this == that); }
      bool operator== (const Quark& q) const { return impl == q.impl; }
      bool operator!= (const Quark& q) const { return impl != q.impl; }
      bool operator< (const Quark& q) const { return impl < q.impl; }
      bool operator! () const { return empty(); }
      std::string to_string () const {
        std::string s;
        if (impl) s = *impl->view;
        return s;
      }
      const StringView& to_view () const {
        static const StringView empty;
        return impl ? *impl->view : empty;
      }
      const char* c_str () const { return impl ? impl->view->str : NULL; }
      operator const char* () const { return c_str(); }
      //operator const std::string () const { return to_string(); }

      /** Number of unique strings being mapped.
          Included for debugging and regression tests... */
      static unsigned long size () { return _lookup.size(); }
      static void dump (std::ostream&);
  };

  std::ostream& operator<< (std::ostream& os, const Quark& s);

  typedef std::set<Quark> quarks_t;

  /**
   * StrictWeakOrdering which sorts Quarks alphabetically.
   * This should be used sparingly, as it defeats Quark's main speed advantage.
   */
  struct AlphabeticalQuarkOrdering {
    bool operator() (const Quark& a, const Quark& b) const {
      if (a.empty() && b.empty()) return false;
      if (a.empty()) return true;
      if (b.empty()) return false;
      return std::strcmp (a.c_str(), b.c_str()) < 0;
    }
    bool operator() (const Quark* a, const Quark* b) const {
      return (*this) (*a, *b);
    }
  };
}

#endif
