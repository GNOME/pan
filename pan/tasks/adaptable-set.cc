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


/* since this is a template file included from a header,
   treat it like a header w.r.t. not including config.h.
   #include <config.h> */
#include <algorithm>
#include <cassert>
#include <ostream>
#include <map>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/tasks/adaptable-set.h>

using namespace pan;

template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: add_top (const std::vector<X>& addme)
{
  items_t items (addme.begin(), addme.end());
  const int pos (0);
  _items.insert (_items.begin()+pos, items.begin(), items.end());
  fire_items_added (items, pos);
}

template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: add_bottom (const std::vector<X>& addme)
{
  items_t items (addme.begin(), addme.end());
  const int pos (_items.size());
  _items.insert (_items.end(), items.begin(), items.end());
  fire_items_added (items, pos);
}

template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: add (const std::vector<X>& addme)
{
  const StrictWeakOrdering sorter;

  // sort the items we're adding
  typedef std::vector<X> xes_t;
  xes_t tmp (addme);
  std::sort (tmp.begin(), tmp.end(), sorter);

  // fold them w/unmoved to find where to insert them in 'items'
  typedef std::map<X,xes_t> add_beforeme_t;
  add_beforeme_t add_beforeme;
  typename xes_t::iterator tmp_it (tmp.begin());
  for (typename xes_t::const_iterator unmoved_it (_unmoved.begin()), end(_unmoved.end()); (unmoved_it!=end) && (tmp_it!=tmp.end()); ) {
    if (sorter (*unmoved_it, *tmp_it))
      ++unmoved_it;
    else
      add_beforeme[*unmoved_it].push_back (*tmp_it++);
  }

#if 0
for (typename add_beforeme_t::const_iterator it(add_beforeme.begin()), end(add_beforeme.end()); it!=end; ++it) {
  std::cerr << "add before " << it->first << ": ";
  for (typename xes_t::const_iterator xit(it->second.begin()), xend(it->second.end()); xit!=xend; ++xit)
    std::cerr << *xit << ' ';
  std::cerr << std::endl;
}
#endif

  // add the new items to 'items'...
  for (typename add_beforeme_t::iterator it(add_beforeme.begin()), e(add_beforeme.end()); it!=e; ++it) {
    typename items_t::iterator iit (std::find (_items.begin(), _items.end(), it->first));
    const int index (std::distance (_items.begin(), iit));
    _items.insert (iit, it->second.begin(), it->second.end());
    fire_items_added (it->second, index);
  }

  // are there any to add after the end?
  if (tmp_it != tmp.end())  {
    const int index (_items.size());
    _items.insert (_items.end(), tmp_it, tmp.end());
    items_t leftovers (tmp_it, tmp.end());
    fire_items_added (leftovers, index);
  }

  // now that we've done polluting `items',
  // adding the new items with `unmoved' is
  // is a fast merge of two sorted lists...
  xes_t tmp2;
  std::merge (tmp.begin(), tmp.end(), _unmoved.begin(), _unmoved.end(), inserter (tmp2, tmp2.begin()), StrictWeakOrdering());
  _unmoved.swap (tmp2);
}

template<class X, class StrictWeakOrdering>
int AdaptableSet<X, StrictWeakOrdering> :: add (X& x)
{
  std::vector<X> tmp;
  tmp.push_back (x);
  add (tmp);
  return std::distance (_items.begin(), std::find(_items.begin(), _items.end(), x));
}

template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: remove_from_unmoved (const X& x)
{
  typedef std::pair<typename items_t::iterator,typename items_t::iterator> pair_t;
  pair_t pair (std::equal_range (_unmoved.begin(), _unmoved.end(), x, _comp));
  typename items_t::iterator it (std::find (pair.first, pair.second, x));
  if (it != pair.second)
    _unmoved.erase (it);
}

template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: remove (int index)
{
  if (0<=index && index<size())
  {
    typename items_t::iterator it (_items.begin() + index);
    X x (*it);
    remove_from_unmoved (x);
    _items.erase (it);
    fire_item_removed (x, index);
  }
}

template<class X, class StrictWeakOrdering>
int AdaptableSet<X, StrictWeakOrdering>:: index_of (const X& x) const
{
  typename items_t::const_iterator it (std::find (_items.begin(), _items.end(), x));
  const int index (it==_items.end() ? -1 : std::distance (_items.begin(), it));
  return index;
}


template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: move (int new_index, int old_index)
{
  assert (0<=new_index && new_index<size());
  assert (0<=old_index && old_index<size());

  if (new_index != old_index)
  {
    X value (_items[old_index]);
    _items.erase  (_items.begin()+old_index);
    _items.insert (_items.begin()+new_index, value);
    remove_from_unmoved (value);
    fire_item_moved (value, new_index, old_index);
  }
}
template<class X, class StrictWeakOrdering> void AdaptableSet<X, StrictWeakOrdering> :: move_up (int index)
{
  if (index > 0)
    move (index-1, index);
}
template<class X, class StrictWeakOrdering> void AdaptableSet<X, StrictWeakOrdering> :: move_top (int index)
{
  if (index > 0)
    move (0, index);
}
template<class X, class StrictWeakOrdering> void AdaptableSet<X, StrictWeakOrdering> :: move_down (int index)
{
  if (0<=index && index+1<size())
    move (index+1, index);
}
template<class X, class StrictWeakOrdering> void AdaptableSet<X, StrictWeakOrdering> :: move_bottom (int index)
{
  if (0<=index && index+1<size())
    move (size()-1, index);
}



template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: fire_items_added (items_t& x, int index)
{
  typedef typename listeners_t::iterator lit;
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_set_items_added (*this, x, index);
}
template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: fire_item_removed (X& x, int index)
{
  typedef typename listeners_t::iterator lit;
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_set_item_removed (*this, x, index);
}
template<class X, class StrictWeakOrdering>
void AdaptableSet<X, StrictWeakOrdering> :: fire_item_moved (X& x, int new_index, int old_index)
{
  typedef typename listeners_t::iterator lit;
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_set_item_moved (*this, x, new_index, old_index);
}
