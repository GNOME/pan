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

#ifndef _AdaptableSet_h_
#define _AdaptableSet_h_

#include <set>
#include <vector>

namespace pan
{
  /**
   * A std::set-like container that allows items to be reordered..
   *
   * The intent is for newly-added elements to be inserted into the
   * proper ordered place but to let callers rearrange elements at will.
   * The subset of unmoved elements will remain ordered.
   *
   * This is used by the task queue so that users can rearrange tasks,
   * but newly-added tasks will be added in a best-fit manner.
   *
   * @ingroup tasks
   */
  template <class X, class StrictWeakOrdering> class AdaptableSet
  {
    public:
      typedef std::vector<X> items_t;
    protected:
      items_t _items;
    private:
      items_t _unmoved;
      void remove_from_unmoved (const X& x);
      const StrictWeakOrdering _comp;

    public:
      typedef typename items_t::iterator iterator;
      typedef typename items_t::const_iterator const_iterator;
      const_iterator begin() const { return _items.begin(); }
      iterator begin() { return _items.begin(); }
      const_iterator end() const { return _items.end(); }
      iterator end() { return _items.end(); }

    public:
      AdaptableSet () {}
      virtual ~AdaptableSet () {}
 
    public:
      bool empty() const { return _items.empty(); }
      int size() const { return _items.size(); }
      const X& operator[](int i) const { return *(_items.begin()+i); }

    public:
      int index_of (const X& x) const;
      void remove      (int index);
      void move_up     (int index);
      void move_down   (int index);
      void move_top    (int index);
      void move_bottom (int index);
      int add (X&);
      void add        (const std::vector<X>&);
      void add_top    (const std::vector<X>&);
      void add_bottom (const std::vector<X>&);
      void move (int new_index, int old_index);

    public:
      /**
       * Interface class for objects that listen to an AdaptableSet's events.
       */
      struct Listener {
        virtual ~Listener () {}
        virtual void on_set_items_added   (AdaptableSet&, items_t&, int index) = 0;
        virtual void on_set_item_removed (AdaptableSet&, X&, int index) = 0;
        virtual void on_set_item_moved   (AdaptableSet&, X&, int index, int old_index) = 0;
      };
      void add_listener (Listener * l) { _listeners.insert (l); }
      void remove_listener (Listener * l) { _listeners.erase (l); }

    protected:
      virtual void fire_items_added   (items_t&, int index);
      virtual void fire_item_removed (X&, int index);
      virtual void fire_item_moved   (X&, int index, int old_index);

    private:
      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;
  };
}

#include "adaptable-set.cc"

#endif
