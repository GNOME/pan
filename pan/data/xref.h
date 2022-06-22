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

#ifndef __Xref_h__
#define __Xref_h__

#include <stdint.h>
#include <algorithm>
#include <pan/general/quark.h>
#include <pan/general/sorted-vector.h>
#include <pan/general/article_number.h>

namespace pan
{
  /**
   * A set of identifiers for an article.
   *
   * By using this as a cross-reference to all the occurences of an article,
   * we can mark every instance of a crosspost as read when it's read in one
   * group, or we can look for an article across different servers.
   *
   * There can only be one entry per [server + group] in a set.
   * Adding a second entry with the same [server + group] will fail.
   *
   * @ingroup data
   */
  class Xref
  {
    public:

      /** Tuple of [server,group,number] describing an article's location. */
      struct Target
      {
        Quark server;
        Quark group;
        Article_Number number;

        Target (): number(0ul) { }
        bool operator== (const Target& t) const
          { return t.server==server && t.group==group && t.number==number; }
        bool operator< (const Target& t) const {
          if (server != t.server) return server < t.server;
          if (group != t.group) return group < t.group;
          return false;
        }
        Target (const Quark& sq, const Quark& gq, Article_Number n):
          server (sq), group (gq), number (n) { }
      };

    public:
      typedef sorted_vector<Target,true> targets_t;
      typedef targets_t::const_iterator const_iterator;
      const_iterator begin() const { return targets.begin(); }
      const_iterator end() const { return targets.end(); }

    public:
      unsigned long size () const { return targets.size(); }
      bool empty () const { return targets.empty(); }
      Article_Number find_number (const Quark& server, const Quark& group) const;
      bool find (const Quark& server, Quark& setme_group, Article_Number& setme_number) const;
      bool has_server (const Quark& server) const;
      void get_servers (quarks_t& addme) const;

    public:
      void clear () { targets.clear(); }
      void remove_server (const Quark& server);
      void remove_targets_less_than (const Quark& s, const Quark& g, Article_Number less_than_this);

    public:

      template<typename ForwardIterator> void insert (ForwardIterator a, ForwardIterator b) {
        targets.insert (a, b);
      }
      void insert (const Target& target) { targets.insert (target); }
      void insert (const Quark& s, const Quark& g, Article_Number n) {targets.insert (Target(s,g,n));}
      void insert (const Xref& xref) { insert (xref.begin(), xref.end()); }

      template<typename ForwardIterator> void assign (ForwardIterator a, ForwardIterator b) {
        targets.clear ();
        targets.insert (a, b);
      }

      void insert (const Quark& s, const StringView& header);
      void swap (targets_t& t) { targets.swap (t); }

    private:
      targets_t targets;
  };
}

#endif
