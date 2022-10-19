#include <config.h>
#include <algorithm>
#include <iostream>
#include <pan/general/test.h>
#include "adaptable-set.h"

using namespace pan;

template<class X> class MyLessThan {
  public:
    MyLessThan () { }
    bool operator() (const X& x, const X& y) const { return x < y; }
};

template<class X>
class MyListener:
  public AdaptableSet<X, MyLessThan<X> >::Listener
{
  public:
    X value;
    int index_of_added;
    int index_of_removed;
    int old_index_of_moved;
    int new_index_of_moved;

  public:
    void on_set_items_added (AdaptableSet<X, MyLessThan<X> >&, std::vector<X>& i, int index) override
    {
      index_of_added = index;
      value = i[0];
    }
    void on_set_item_removed (AdaptableSet<X, MyLessThan<X> >&, X& i, int index) override {
      index_of_removed = index;
      value = i;
    }
    void on_set_item_moved (AdaptableSet<X, MyLessThan<X> >&, X& i, int index, int old_index) override
    {
      old_index_of_moved = old_index;
      new_index_of_moved = index;
      value = i;
    }
    void clear () {
      index_of_added = index_of_removed = old_index_of_moved = new_index_of_moved = -1;
    }
    MyListener () : value() {
      clear ();
    }
    bool empty () const {
      return index_of_added==-1
          && index_of_removed==-1
          && old_index_of_moved==-1
          && new_index_of_moved==-1;
    }
};

typedef AdaptableSet<int, MyLessThan<int> > MyIntSet;
int main ()
{
  int i;
  MyIntSet s;
  MyListener<int> l;
  check (l.empty())
  s.add_listener (&l);

  check (s.empty())

  // trivial check: add to an empty set
  l.clear ();
  s.add (i = 10);
  check (l.value == 10)
  check (l.index_of_added == 0)
  check (!s.empty())
  check (s.size() == 1)
  check (s[0] == 10)

  // another add: does it insert in the right place?
  l.clear ();
  s.add (i = 30);
  check (l.index_of_added == 1)
  check (l.value == 30)
  check (s.size() == 2)
  check (s[0] == 10)
  check (s[1] == 30)

  // another add: does it insert in the right place?
  l.clear ();
  s.add (i = 20);
  check (l.index_of_added == 1)
  check (l.value == 20)
  check (s.size() == 3)
  check (s[0] == 10)
  check (s[1] == 20)
  check (s[2] == 30)

  // testing move_up
  l.clear ();
  MyIntSet s2 (s);
  s.move_up (2);
  check (l.old_index_of_moved == 2)
  check (l.new_index_of_moved == 1)
  check (l.value == 30)
  check (s.size() == 3)
  check (s[0] == 10)
  check (s[1] == 30)
  check (s[2] == 20)
  l.clear ();
  s.move_up (1);
  check (s.size() == 3)
  check (l.old_index_of_moved == 1)
  check (l.new_index_of_moved == 0)
  check (l.value == 30)
  check (s[0] == 30)
  check (s[1] == 10)
  check (s[2] == 20)
  l.clear ();
  s.move_up (i = 0);
  check (l.empty ())
  check (s.size() == 3)
  check (s[0] == 30)
  check (s[1] == 10)
  check (s[2] == 20)

  // does the set ignore moved items when finding the insertion point?
  // the unmoved set is 10, 20
  l.clear ();
  s.add (i = 15);
  check (l.index_of_added == 2)
  check (l.value == 15)
  check (s.size() == 4)
  check (s[0] == 30)
  check (s[1] == 10)
  check (s[2] == 15)
  check (s[3] == 20)

  // does the set ignore moved items when finding the insertion point?
  // the unmoved set is 10, 15, 20
  l.clear ();
  s.add (i = 21);
  check (l.index_of_added == 4)
  check (l.value == 21)
  check (s.size() == 5)
  check (s[0] == 30)
  check (s[1] == 10)
  check (s[2] == 15)
  check (s[3] == 20)
  check (s[4] == 21)

  // does the set ignore moved items when finding the insertion point?
  // the unmoved set is 10, 15, 20, 21
  l.clear ();
  s.add (i = 31);
  check (s.size() == 6)
  check (l.index_of_added == 5)
  check (l.value == 31)
  check (s[0] == 30)
  check (s[1] == 10)
  check (s[2] == 15)
  check (s[3] == 20)
  check (s[4] == 21)
  check (s[5] == 31)

  // testing move_down
  l.clear ();
  s.move_down (1);
  check (l.old_index_of_moved == 1)
  check (l.new_index_of_moved == 2)
  check (l.value == 10)
  check (s.size() == 6)
  check (s[0] == 30)
  check (s[1] == 15)
  check (s[2] == 10)
  check (s[3] == 20)
  check (s[4] == 21)
  check (s[5] == 31)
  l.clear ();
  s.move_down (2);
  check (l.old_index_of_moved == 2)
  check (l.new_index_of_moved == 3)
  check (l.value == 10)
  check (s.size() == 6)
  check (s[0] == 30)
  check (s[1] == 15)
  check (s[2] == 20)
  check (s[3] == 10)
  check (s[4] == 21)
  check (s[5] == 31)

  // does the set ignore moved items when finding the insertion point?
  // the unmoved set is 15, 20, 21, 31
  l.clear ();
  s.add (i = 11);
  check (l.index_of_added == 1)
  check (l.value == 11)
  check (s.size() == 7)
  check (s[0] == 30)
  check (s[1] == 11)
  check (s[2] == 15)
  check (s[3] == 20)
  check (s[4] == 10)
  check (s[5] == 21)
  check (s[6] == 31)

  // move to bottom...
  l.clear ();
  s.move_bottom (3);
  check (l.value == 20)
  check (l.old_index_of_moved == 3)
  check (l.new_index_of_moved == 6)
  check (s.size() == 7)
  check (s[0] == 30)
  check (s[1] == 11)
  check (s[2] == 15)
  check (s[3] == 10)
  check (s[4] == 21)
  check (s[5] == 31)
  check (s[6] == 20)

  // test another add after move_bottom()
  // does the set ignore moved items when finding the insertion point?
  // the unmoved set is 15, 21, 31
  l.clear ();
  s.add (i = 19);
  check (l.value == 19)
  check (l.index_of_added == 4)
  check (s.size() == 8)
  check (s[0] == 30)
  check (s[1] == 11)
  check (s[2] == 15)
  check (s[3] == 10)
  check (s[4] == 19)
  check (s[5] == 21)
  check (s[6] == 31)
  check (s[7] == 20)

  // now for multiple insert tests...
  MyIntSet m;
  l.clear ();
  std::vector<int> addme;
  addme.push_back (5);
  addme.push_back (10);
  addme.push_back (15);
  addme.push_back (20);
  m.add (addme);
  check (m.size() == 4)
  check (m[0] == 5)
  check (m[1] == 10)
  check (m[2] == 15)
  check (m[3] == 20)

  // start with an easy addition...
  addme.clear ();
  addme.push_back (0);
  addme.push_back (1);
  addme.push_back (2);
  m.add (addme);
  check (m.size() == 7)
  check (m[0] == 0)
  check (m[1] == 1)
  check (m[2] == 2)
  check (m[3] == 5)
  check (m[4] == 10)
  check (m[5] == 15)
  check (m[6] == 20)

  // now a more complicated one...
  addme.clear ();
  addme.push_back (12);
  addme.push_back (8);
  addme.push_back (17);
  addme.push_back (25);
  m.add (addme);
  check (m.size() == 11)
  check (m[0] == 0)
  check (m[1] == 1)
  check (m[2] == 2)
  check (m[3] == 5)
  check (m[4] == 8)
  check (m[5] == 10)
  check (m[6] == 12)
  check (m[7] == 15)
  check (m[8] == 17)
  check (m[9] == 20)
  check (m[10] == 25)

  // now after we've moved some items...
  m.move_up (3);
  m.move_up (7);
  check (m.size() == 11)
  check (m[0] == 0)
  check (m[1] == 1)
    check (m[2] == 5)
  check (m[3] == 2)
  check (m[4] == 8)
  check (m[5] == 10)
    check (m[6] == 15)
  check (m[7] == 12)
  check (m[8] == 17)
  check (m[9] == 20)
  check (m[10] == 25)
  addme.clear ();
  addme.push_back (3);
  addme.push_back (11);
  addme.push_back (14);
  addme.push_back (30);
  m.add (addme);
  check (m.size() == 15)
  check (m[0] == 0)
  check (m[1] == 1)
    check (m[2] == 5)
  check (m[3] == 2)
  check (m[4] == 3)
  check (m[5] == 8)
  check (m[6] == 10)
    check (m[7] == 15)
  check (m[8] == 11)
  check (m[9] == 12)
  check (m[10] == 14)
  check (m[11] == 17)
  check (m[12] == 20)
  check (m[13] == 25)
  check (m[14] == 30)

  return 0;
}
