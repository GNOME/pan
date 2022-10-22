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

#include <config.h>
#include <iostream>
#include <algorithm>
#include <cstdarg>
#include <set>
#include <gobject/gvaluecollector.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include "pan-tree.h"

#define IS_SORTED(tree) \
  (tree->sort_column_id != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID)

/***
****  Row Helper Functions
***/

void
PanTreeStore :: Row :: set_value_int (GValue * setme, int value)
{
  g_value_init (setme, G_TYPE_INT);
  g_value_set_int (setme, value);
}

void
PanTreeStore :: Row :: set_value_pointer (GValue * setme, gpointer value)
{
  g_value_init (setme, G_TYPE_POINTER);
  g_value_set_pointer (setme, value);
}

void
PanTreeStore :: Row :: set_value_ulong (GValue * setme, unsigned long value)
{
  g_value_init (setme, G_TYPE_ULONG);
  g_value_set_ulong (setme, value);
}

void
PanTreeStore :: Row :: set_value_string (GValue * setme, const char * value)
{
  g_value_init (setme, G_TYPE_STRING);
  g_value_set_string (setme, value);
}

void
PanTreeStore :: Row :: set_value_static_string (GValue * setme, const char * value)
{
  g_value_init (setme, G_TYPE_STRING);
  g_value_set_static_string (setme, value);
}


/***
****  Low-level utilities
***/

GtkTreeIter
PanTreeStore :: get_iter (const Row * row)
{
  g_assert (row);
  GtkTreeIter setme;
  set_iter (&setme, row);
  return setme;
}

void
PanTreeStore :: get_iter (const Row * row, GtkTreeIter * setme)
{
  g_assert (setme);
  g_assert (row);
  set_iter (setme, row);
}


PanTreeStore :: Row*
PanTreeStore :: get_row (GtkTreeIter * iter)
{
  g_assert (iter);
  g_assert (iter->stamp == stamp);
  Row * row (static_cast<Row*>(iter->user_data));
  g_assert (row);
  return row;
}

const PanTreeStore :: Row*
PanTreeStore :: get_row (const GtkTreeIter * iter) const
{
  g_assert (iter);
  g_assert (iter->stamp == stamp);
  const Row * row (static_cast<const Row*>(iter->user_data));
  g_assert (row);
  return row;
}

void
PanTreeStore :: set_iter (GtkTreeIter * iter,
                          const Row   * row)
{
  iter->stamp = stamp;
  iter->user_data = (gpointer)row;
  iter->user_data2 = nullptr;
  iter->user_data3 = nullptr;
}

void
PanTreeStore :: invalidate_iter (GtkTreeIter * iter)
{
  iter->stamp      = 0;
  iter->user_data  = (void*) 0xDEADBEEF;
  iter->user_data2 = (void*) 0xDEADBEEF;
  iter->user_data2 = (void*) 0xDEADBEEF;
}

bool
PanTreeStore :: set_or_invalidate (GtkTreeIter * iter, const Row * row)
{
  if (row)
    set_iter (iter, row);
  else
    invalidate_iter (iter);
  return row != nullptr;
}


/*****
******
******  implementing GtkTreeModel's interface
******
*****/

GtkTreeModelFlags
PanTreeStore :: model_get_flags (GtkTreeModel *)
{
  return GTK_TREE_MODEL_ITERS_PERSIST;
}

gint
PanTreeStore :: model_get_n_columns (GtkTreeModel * model)
{
  const PanTreeStore * store (PAN_TREE_STORE(model));
  g_return_val_if_fail (store, 0);
  return store->n_columns;
}

GType
PanTreeStore :: model_get_column_type (GtkTreeModel * tree,
                                       gint           n)
{
  return (*((PanTreeStore*)(tree))->column_types)[n];
}

gboolean
PanTreeStore :: model_get_iter (GtkTreeModel * model,
                                GtkTreeIter  * setme,
                                GtkTreePath  * path)
{
  PanTreeStore * store = PAN_TREE_STORE(model);
  g_return_val_if_fail (store, false);
  g_return_val_if_fail (path, false);

  // make sure it's not an empty path.
  const int depth (gtk_tree_path_get_depth (path));
  if (depth < 1)
    return false;

  // find the row that correpsonds to this path.
  PanTreeStore::Row * row (store->root);
  const int * indices = gtk_tree_path_get_indices (path);
  for (int i=0; i<depth; ++i) {
    row = row->nth_child (*indices++);
    if (!row)
      return false;
  }

  // build an iter from that row.
  store->set_iter (setme, row);
  return true;
}

GtkTreePath*
PanTreeStore :: model_get_path (GtkTreeModel * model,
                                GtkTreeIter  * iter)
{
  PanTreeStore * store (PAN_TREE_STORE(model));
  g_return_val_if_fail (store, nullptr);
  return store->get_path (iter);
}

void
PanTreeStore :: model_get_value (GtkTreeModel * model,
                                 GtkTreeIter  * iter,
                                 gint           column,
                                 GValue       * dest_value)
{
  g_assert (iter);
  g_assert (dest_value);
  PanTreeStore * store = PAN_TREE_STORE(model);
  g_assert (store);
  g_assert (iter->stamp == store->stamp);
  g_assert (0<=column && column<store->n_columns);

  store->get_row(iter)->get_value (column, dest_value);
}

gboolean
PanTreeStore :: model_iter_next (GtkTreeModel * model,
                                 GtkTreeIter  * iter)
{
  PanTreeStore * tree = PAN_TREE_STORE(model);
  g_return_val_if_fail (tree, false);
  g_return_val_if_fail (iter, false);
  g_return_val_if_fail (iter->stamp == tree->stamp, false);

  Row * row (tree->get_row (iter));
  row = row->parent->nth_child (row->child_index + 1);

  return tree->set_or_invalidate (iter, row);
}

gboolean
PanTreeStore :: model_iter_children (GtkTreeModel * model,
                                     GtkTreeIter  * iter,
                                     GtkTreeIter  * parent)
{
  return model_iter_nth_child (model, iter, parent, 0);
}

gint
PanTreeStore :: model_iter_n_children (GtkTreeModel * model,
                                       GtkTreeIter  * iter)
{
  PanTreeStore * tree = PAN_TREE_STORE(model);
  g_return_val_if_fail (tree, 0);
  g_return_val_if_fail (!iter || iter->stamp == tree->stamp, 0);

  const Row * row (iter ? tree->get_row(iter) : tree->root);
  return row->n_children();
}

gboolean
PanTreeStore :: model_iter_has_child (GtkTreeModel *model,
                                      GtkTreeIter  *iter)
{
  return model_iter_n_children (model, iter) != 0;
}

gboolean
PanTreeStore :: model_iter_nth_child (GtkTreeModel * model,
                                      GtkTreeIter  * iter,
                                      GtkTreeIter  * parent,
                                      gint           n)
{
  PanTreeStore * tree = PAN_TREE_STORE(model);
  g_return_val_if_fail (tree, false);
  g_return_val_if_fail (iter, false);
  g_return_val_if_fail (!parent || parent->stamp == tree->stamp, false);

  Row * row (parent ? tree->get_row(parent) : tree->root);
  row = row->nth_child (n);

  return tree->set_or_invalidate (iter, row);
}

gboolean
PanTreeStore :: model_iter_parent (GtkTreeModel * model,
                                   GtkTreeIter  * iter,
                                   GtkTreeIter  * child)
{
  return PAN_TREE_STORE(model)->get_parent (iter, child);
}

/*****
******
*****/

GtkTreePath*
PanTreeStore :: get_path (const Row * row) const
{
  g_return_val_if_fail (row, NULL);

  std::vector<int> indices;
  while (row && row!=root) {
    indices.push_back (row->child_index);
    row = row->parent;
  }

  GtkTreePath * path = gtk_tree_path_new ();
  for (std::vector<int>::const_reverse_iterator it(indices.rbegin()), end(indices.rend()); it!=end; ++it)
    gtk_tree_path_append_index (path, *it);

  return path;
}

GtkTreePath*
PanTreeStore :: get_path (GtkTreeIter  * iter)
{
  g_return_val_if_fail (iter, NULL);
  g_return_val_if_fail (iter->stamp == stamp, NULL);

  return get_path (get_row (iter));
}

bool
PanTreeStore :: get_parent (GtkTreeIter * iter,
                            GtkTreeIter * child)
{
  g_return_val_if_fail (child, false);
  g_return_val_if_fail (iter, false);
  g_return_val_if_fail (child->stamp == stamp, false);

  const Row * row (get_row (child));
  return set_or_invalidate (iter, row->parent!=root ? row->parent : nullptr);
}

bool
PanTreeStore :: is_root (const GtkTreeIter* iter) const
{
  g_return_val_if_fail (iter, false);
  g_return_val_if_fail (iter->stamp == stamp, false);
  return get_row(iter)->parent == root;
}

size_t
PanTreeStore :: get_depth (const Row * row) const
{
  g_assert (row);
  size_t depth (0);
  for (;;) {
    if (row == root) break;
    ++depth;
    row = row->parent;
    g_assert (row);
  }
  return depth;
}

bool
PanTreeStore :: is_in_tree (Row * row) const
{
  while (row && row!=root)
    row = row->parent;
  return row == root;
}

/*****
******
******  implementing GtkTreeSortable
******
*****/

gboolean
PanTreeStore :: sortable_get_sort_column_id (GtkTreeSortable  * sortable,
                                             gint             * setme_column_id,
                                             GtkSortType      * setme_order)
{
  PanTreeStore * tree (PAN_TREE_STORE (sortable));
  g_return_val_if_fail (tree, false);

  if (setme_column_id)
     *setme_column_id = tree->sort_column_id;
  if (setme_order)
     *setme_order = tree->order;

  return (tree->sort_column_id != GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID)
      && (tree->sort_column_id != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID);
}

gboolean
PanTreeStore :: sortable_has_sort_func (GtkTreeSortable *sortable, gint col)
{
  PanTreeStore * store (PAN_TREE_STORE (sortable));
  g_return_val_if_fail (store, false);
  column_sort_info_t::const_iterator it (store->sort_info->find (col));
  return (it!=store->sort_info->end()) && (it->second.sort_func!=nullptr);
}

// SORTING

struct
PanTreeStore :: SortRowInfo
{
  int pos;
  Row * row;
  GtkTreeIter iter;
};

struct PanTreeStore :: SortData
{
  PanTreeStore * store;
  GtkTreeModel * model;
  PanTreeStore::SortInfo& sort_info;
  GtkSortType order;
  SortData (PanTreeStore           * tree_store,
            PanTreeStore::SortInfo & s,
            GtkSortType              o):
    store(tree_store), model(GTK_TREE_MODEL(tree_store)), sort_info(s), order(o) {}
};

int
PanTreeStore :: row_compare_func (gconstpointer a_gpointer,
                                  gconstpointer b_gpointer,
                                  gpointer      user_data)
{
  const SortRowInfo * a = (const SortRowInfo*) a_gpointer;
  const SortRowInfo * b = (const SortRowInfo*) b_gpointer;
  const SortData * help = (const SortData*) user_data;
  int val = (help->sort_info.sort_func) (help->model,
                                         const_cast<GtkTreeIter*>(&a->iter),
                                         const_cast<GtkTreeIter*>(&b->iter),
                                         help->sort_info.user_data);
  if (!val) // inplace sort
    val = a->row->child_index - b->row->child_index;

  if (help->order == GTK_SORT_DESCENDING)
    val = -val;

  return val;
}

void
PanTreeStore :: sort_children (SortInfo  & sort_info,
                               Row       * parent,
                               bool        recurse,
                               int         mode)
{
  g_assert (parent);
  g_assert (mode==FLIP || mode==SORT);

  const int n (parent->n_children());
  if (n < 2) // no need to sort
    return;

  // build a temporary array to sort
  SortRowInfo * sort_array = new SortRowInfo [n];
  for (int i=0; i<n; ++i) {
    SortRowInfo& row_info (sort_array[mode==SORT ? i : n-1-i]);
    row_info.pos = i;
    row_info.row = parent->children[i];
    set_iter (&row_info.iter, row_info.row);
  }

  if (mode==SORT) {
    SortData help (this, sort_info, order);
    g_qsort_with_data (sort_array,
		       n, sizeof(SortRowInfo),
		       row_compare_func, &help);
  }

  // update the child indices...
  bool reordered (false);
  for (int i=0; i<n; ++i) {
    Row * child = sort_array[i].row;
    reordered |= (child->child_index != i);
    child->child_index = i;
    parent->children[i] = child;
    g_assert (child->parent == parent);
  }

  // let the world know we've changed
  if (reordered)
  {
    int * new_order (new int [n]);
    for (int i=0; i<n; ++i)
      new_order[i] = sort_array[i].pos;

    GtkTreeModel * model (GTK_TREE_MODEL(this));
    if (parent == root)
    {
      GtkTreePath * path (gtk_tree_path_new ());
      gtk_tree_model_rows_reordered (model, path, nullptr, new_order);
      gtk_tree_path_free (path);
    }
    else
    {
      GtkTreeIter it (get_iter (parent));
      GtkTreePath * path (get_path (parent));
      gtk_tree_model_rows_reordered (model, path, &it, new_order);
      gtk_tree_path_free (path);
    }

    delete [] new_order;
  }

  delete [] sort_array;

  for (int i=0; recurse && i<n; ++i)
    sort_children (sort_info, parent->children[i], recurse, mode);
}

void
PanTreeStore :: sort (int mode)
{
  if (!sort_paused && (sort_column_id != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID))
  {
    g_assert (sortable_has_sort_func (GTK_TREE_SORTABLE(this), sort_column_id));
    sort_children ((*sort_info)[sort_column_id], root, true, mode);
  }
}

void
PanTreeStore :: sortable_set_sort_column_id (GtkTreeSortable * sortable,
                                             gint              sort_column_id,
                                             GtkSortType       order)
{
  PanTreeStore * tree (PAN_TREE_STORE (sortable));
  g_return_if_fail (tree);

  // if no change, there's nothing to do...
  if (tree->sort_column_id==sort_column_id && tree->order==order)
    return;

  // sanity checks...
  if (sort_column_id != GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID) {
    g_return_if_fail (tree->sort_info->count(sort_column_id) != 0);
    g_return_if_fail (tree->sort_info->find(sort_column_id)->second.sort_func != nullptr);
  }

  const bool flip (sort_column_id == tree->sort_column_id);
  tree->sort_paused = 0;
  tree->sort_column_id = sort_column_id;
  tree->order = order;
  gtk_tree_sortable_sort_column_changed (sortable);
  tree->sort (flip ? FLIP : SORT);
}

gboolean
PanTreeStore :: sortable_has_default_sort_func (GtkTreeSortable *sortable)
{
  return sortable_has_sort_func (sortable, GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID);
}

void
PanTreeStore :: sortable_set_sort_func (GtkTreeSortable        *sortable,
                                        gint                    col,
                                        GtkTreeIterCompareFunc  func,
                                        gpointer                data,
                                        GDestroyNotify          destroy)
{
  PanTreeStore * tree (PAN_TREE_STORE (sortable));
  g_return_if_fail (tree);

  (*tree->sort_info)[col].assign (func, data, destroy);

  if (tree->sort_column_id == col)
    tree->sort ();
}

void
PanTreeStore :: sortable_set_default_sort_func (GtkTreeSortable        * s,
                                                GtkTreeIterCompareFunc   f,
                                                gpointer                 p,
                                                GDestroyNotify           d)
{
  sortable_set_sort_func (s, GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID, f, p, d);
}

namespace
{
  GObjectClass *parent_class (nullptr);

  typedef std::vector<GValue> values_t;
}

struct
PanTreeStore :: FreeRowWalker: public PanTreeStore::WalkFunctor
{
  PanTreeStore * store;
  FreeRowWalker (PanTreeStore *t): store(t) {}
  virtual ~FreeRowWalker () {}
  bool operator()(PanTreeStore *s, Row *r, GtkTreeIter*, GtkTreePath*) override
  {
    if (store->row_dispose)
        store->row_dispose->row_dispose (s, r);
    else
        delete r;
    return true; // keep marching
  }
};

void
PanTreeStore :: pan_tree_dispose (GObject *object)
{
  PanTreeStore * store (PAN_TREE_STORE (object));

  // erase the remaining nodes
  FreeRowWalker walk (store);
  store->postfix_walk (walk);

  // clear the sort info
  store->sort_info->clear ();
}

void
PanTreeStore :: pan_tree_finalize (GObject *object)
{
  PanTreeStore * store (PAN_TREE_STORE (object));

  delete store->root;
  delete store->column_types;
  delete store->sort_info;
  store->root = nullptr;
  store->column_types = nullptr;
  store->sort_info = nullptr;

  (*parent_class->finalize) (object);
}

void
PanTreeStore :: pan_tree_class_init (PanTreeStoreClass *klass)
{
  GObjectClass *object_class;

  parent_class = (GObjectClass*) g_type_class_peek_parent (klass);
  object_class = (GObjectClass*) klass;

  object_class->dispose = pan_tree_dispose;
  object_class->finalize = pan_tree_finalize;
}

void
PanTreeStore :: pan_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags       = model_get_flags;
  iface->get_n_columns   = model_get_n_columns;
  iface->get_column_type = model_get_column_type;
  iface->get_iter        = model_get_iter;
  iface->get_path        = model_get_path;
  iface->get_value       = model_get_value;
  iface->iter_next       = model_iter_next;
  iface->iter_children   = model_iter_children;
  iface->iter_has_child  = model_iter_has_child;
  iface->iter_n_children = model_iter_n_children;
  iface->iter_nth_child  = model_iter_nth_child;
  iface->iter_parent     = model_iter_parent;
}

void
PanTreeStore :: pan_tree_sortable_init (GtkTreeSortableIface *iface)
{
  iface->get_sort_column_id    = sortable_get_sort_column_id;
  iface->set_sort_column_id    = sortable_set_sort_column_id;
  iface->set_sort_func         = sortable_set_sort_func;
  iface->set_default_sort_func = sortable_set_default_sort_func;
  iface->has_default_sort_func = sortable_has_default_sort_func;
}

namespace
{
  class RootRow: public PanTreeStore::Row {
    void get_value (int, GValue *) override { /* unused */ }
  };
}

void
PanTreeStore :: pan_tree_init (PanTreeStore * tree)
{
  tree->stamp = g_random_int();
  tree->n_columns = 0;
  tree->root = new RootRow ();
  tree->row_dispose = nullptr;
  tree->column_types = new std::vector<GType>();
  tree->sort_paused = 0;
  tree->sort_info = new column_sort_info_t;
  tree->sort_column_id = GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID;
  tree->order = GTK_SORT_ASCENDING;
}

/***
****
***/

GType
PanTreeStore :: get_type ()
{
  static GType pan_tree_type (0);
  if (pan_tree_type)
    return pan_tree_type;

  static const GTypeInfo pan_tree_info = {
    sizeof (PanTreeStoreClass),
    NULL, // base_init
    NULL, // base_finalize
    (GClassInitFunc) pan_tree_class_init,
    NULL, // class finalize
    NULL, // class_data
    sizeof (PanTreeStore),
    0, // n_preallocs
    (GInstanceInitFunc) pan_tree_init,
    nullptr // value_table
  };

  pan_tree_type = g_type_register_static (
    G_TYPE_OBJECT, "PanTreeStore", &pan_tree_info, (GTypeFlags)0);
  static const GInterfaceInfo tree_model_info =
    { (GInterfaceInitFunc)pan_tree_model_init, nullptr, nullptr };
  g_type_add_interface_static (
    pan_tree_type, GTK_TYPE_TREE_MODEL, &tree_model_info);
  static const GInterfaceInfo sortable_info =
    { (GInterfaceInitFunc)pan_tree_sortable_init, nullptr, nullptr };
  g_type_add_interface_static (
    pan_tree_type, GTK_TYPE_TREE_SORTABLE, &sortable_info);
  return pan_tree_type;
}

PanTreeStore*
PanTreeStore :: new_tree (int n_columns, ...)
{
  g_return_val_if_fail (n_columns>0, nullptr);
  PanTreeStore* tree = (PanTreeStore*) g_object_new (PAN_TREE_STORE_TYPE, NULL);

  va_list args;
  va_start (args, n_columns);
  for (int i=0; i<n_columns; ++i) {
    const GType type (va_arg (args, GType));
    tree->column_types->push_back (type);
  }
  va_end (args);
  tree->n_columns = tree->column_types->size();

  return tree;
}

/***
****
***/

void
PanTreeStore :: row_changed (Row * row)
{
  GtkTreeIter iter;
  get_iter (row, &iter);
  row_changed (&iter);
}

void
PanTreeStore :: row_changed (GtkTreeIter * iter)
{
  GtkTreeModel * model (GTK_TREE_MODEL(this));
  GtkTreePath * path (gtk_tree_model_get_path (model, iter));
  gtk_tree_model_row_changed (model, path, iter);
  gtk_tree_path_free (path);
}

/***
****
***/

void
PanTreeStore :: renumber_children (Row * parent,
                                   int   child_lo,
                                   int   child_hi)
{
  const int n_children (parent->n_children());
  child_hi = std::min (child_hi, n_children);

  g_assert (parent);
  g_assert (child_lo >= 0);
  g_assert (child_lo <= child_hi);
  g_assert (child_hi <= n_children);

  Row ** it = &parent->children[child_lo];
  for (int i=child_lo; i!=child_hi; ++i, ++it) {
    (*it)->child_index = i;
    (*it)->parent = parent;
  }
}

struct PanTreeStore :: RowCompareByDepth
{
  PanTreeStore * store;
  RowCompareByDepth (PanTreeStore *s): store(s) {}
  bool operator() (const Row* a, const Row* b) const {
    return store->get_depth(a) < store->get_depth(b);
  }
};

void
PanTreeStore :: insert_sorted (const parent_to_children_t& new_parents)
{
  g_return_if_fail (!new_parents.empty());

  RowCompareByDepth depth_compare (this);

  // this is a pool of all the parents that need kids added...
  std::set<Row*> pool;
  foreach_const (parent_to_children_t, new_parents, it)
    pool.insert (it->first ? it->first : root);

  while (!pool.empty())
  {
    // get a subset of pool: parents that are are in the tree
    rows_t keys;
    foreach_const (std::set<Row*>, pool, it)
      if (is_in_tree (*it))
        keys.push_back (*it);
    g_assert (!keys.empty());

    // sort these from shallowest to deepest
    std::sort (keys.begin(), keys.end(), depth_compare);

    // process each of them
    foreach_const (rows_t, keys, it)
    {
      // add the children to this parent...
      // caller passes in NULL for the parent of top-level articles...
      Row * parent (*it);
      Row * key (parent==root ? nullptr : parent);
      const rows_t& children (new_parents.find(key)->second);
      insert_sorted (parent, children);
      pool.erase (parent);
    }
  }
}

struct PanTreeStore :: RowCompareByColumn
{
  PanTreeStore * store;
  GtkTreeModel * model;
  GtkTreeIterCompareFunc sort_func;
  gpointer user_data;
  bool reverse;

  RowCompareByColumn (PanTreeStore * s):
    store (s),
    model (GTK_TREE_MODEL(s))
  {
    const SortInfo& info ((*(store->sort_info))[store->sort_column_id]);
    sort_func = info.sort_func;
    user_data = info.user_data;
    reverse = (store->order == GTK_SORT_DESCENDING);
  }

  bool operator() (const Row* a, const Row* b) const
  {
    GtkTreeIter a_it (store->get_iter (a));
    GtkTreeIter b_it (store->get_iter (b));
    int val = sort_func ? (sort_func)(model, &a_it, &b_it, user_data) : (a<b);
    if (reverse) val = -val;
    return val < 0;
  }
};

/***
****
***/

struct PanTreeStore::ReparentWalker: public PanTreeStore::WalkFunctor
{
  GtkTreeModel * model;

  ReparentWalker (PanTreeStore *store): model(GTK_TREE_MODEL(store)) {}

  virtual ~ReparentWalker () {}

  bool operator()(PanTreeStore *store, Row* row,
                          GtkTreeIter *iter, GtkTreePath *path) override
  {
    const int n_children (row->n_children());

    if (n_children)
    {
      // fire a "row inserted" for each child
      GtkTreePath * cpath (gtk_tree_path_copy (path));
      gtk_tree_path_down (cpath);
      for (int i=0; i<n_children; ++i) {
        Row * child (row->nth_child (i));
        GtkTreeIter citer;
        store->set_iter (&citer, child);
        gtk_tree_model_row_inserted (model, cpath, &citer);
        if (!i)
          gtk_tree_model_row_has_child_toggled (model, path, iter);
        gtk_tree_path_next (cpath);
      }
      gtk_tree_path_free (cpath);
    }

    return true; // keep marching
  }
};

void
PanTreeStore :: insert_sorted (Row           * parent,
                               const rows_t  & children_in)
{
  g_return_if_fail (!children_in.empty());

  if (!parent)
       parent = root;

//std::cerr << LINE_ID << " adding " << children_in.size() << " sorted children to " << parent << " which currently has " << parent->children.size() << " children" << std::endl;

  // sort new children...
  rows_t children (children_in);
  foreach (rows_t, children, it)
    (*it)->parent = parent;
  RowCompareByColumn compare_column (this);
  std::sort (children.begin(), children.end(), compare_column);

//for (int i=0, n=children.size()-1; i<n; ++i)
 // g_assert (!compare_column (children[i+1], children[i]));
//for (int i=0, n=parent->children.size()-1; i<n; ++i)
 // g_assert (!compare_column (parent->children[i+1], parent->children[i]));



  // merge the two sorted containers together...
  const size_t old_size (parent->children.size());
  rows_t tmp;
  tmp.reserve (old_size + children.size());
  const size_t n_new (children.size());
  std::vector<int> indices;
  indices.reserve (n_new);
  rows_t::const_iterator o_it  (parent->children.begin()),
                         o_end (parent->children.end()),
                         n_it  (children.begin()),
                         n_end (children.end());
  for (size_t i=0; o_it!=o_end || n_it!=n_end; ++i) {
    Row * addme (nullptr);
    if ((n_it==n_end)) {
      //std::cerr << LINE_ID << " n is empty ... adding another o at " << tmp.size() << std::endl;
      addme = *o_it++;
    }
    else if (o_it==o_end) {
      //std::cerr << LINE_ID << " o is empty .. adding NEW at " << tmp.size() << std::endl;
      indices.push_back (i);
      addme = *n_it++;
    }
    else if (compare_column (*n_it, *o_it)) {
      //std::cerr << LINE_ID << " adding NEW at index " << tmp.size() << std::endl;
      indices.push_back (i);
      addme = *n_it++;
    }
    else {
      //std::cerr << LINE_ID << " adding old at index " << tmp.size() << std::endl;
      addme = *o_it++;
    }
  //  g_assert (tmp.empty() || !compare_column(addme,tmp.back()));
    addme->child_index = i;
   // g_assert (tmp.empty() || !compare_column(addme,tmp.back()));
    tmp.push_back (addme);
  }
  parent->children.swap (tmp);

  // check our work...
  //g_assert (parent->children.size() == old_size + children.size());
  //g_assert (indices.size() == children.size());
  //for (int i=0, n=parent->children.size(); i<n; ++i)
   // g_assert (i==0 || !compare_column(parent->children[i], parent->children[i-1]));

  // emit the 'row inserted' signals
  GtkTreePath * path (get_path (parent));
  GtkTreeIter iter;
  GtkTreeModel * model (GTK_TREE_MODEL (this));
  foreach_const (std::vector<int>, indices, it)
  {
    Row * child (parent->nth_child (*it));
    get_iter (child, &iter);
    gtk_tree_path_append_index (path, *it);
    gtk_tree_model_row_inserted (model, path, &iter);
    gtk_tree_path_up (path);

    // if row has children, this handles all of their
    // row-inserted and has-child-toggled signals...
    // if the row doesn't have children, this has
    // no effect...
    if (!child->children.empty()) {
      ReparentWalker walk (this);
      prefix_walk (walk, &iter, true);
    }
  }

  // maybe emit the 'row has child toggled' signal
  if (!old_size && parent!=root) {
    GtkTreeIter it (get_iter (parent));
    gtk_tree_model_row_has_child_toggled (model, path, &it);
  }

  // cleanup
  gtk_tree_path_free (path);

  //for (int i=0, n=parent->children.size(); i<n; ++i)
   // g_assert (i==0 || !compare_column(parent->children[i], parent->children[i-1]));
}

void
PanTreeStore :: insert (Row            * parent_row,
                        const rows_t   & new_children,
                        int              position)
{
  g_return_if_fail (!new_children.empty());
  g_return_if_fail (position >= 0);

  if (!parent_row)
       parent_row = root;

  // insert the rows
  const size_t n_rows (new_children.size());
  const int old_size (parent_row->n_children());
  position = std::min (position, old_size);
  parent_row->children.insert (parent_row->children.begin()+position,
                               new_children.begin(),
                               new_children.end());
  renumber_children (parent_row, position);

  // set the return iter
  GtkTreeIter iter (get_iter (parent_row->children[position]));

  // emit the 'row inserted' signals
  GtkTreeModel * model (GTK_TREE_MODEL(this));
  Row ** it (&parent_row->children[position]);
  GtkTreePath * path (get_path (*it));
  for (size_t i=0; i<n_rows; ++i)
  {
    Row * child (*it++);
    set_iter (&iter, child);
    gtk_tree_model_row_inserted (model, path, &iter);
    gtk_tree_path_next (path);

    // if row has children, this handles all of their
    // row-inserted and has-child-toggled signals...
    // if the row doesn't have children, this has
    // no effect...
    if (!child->children.empty()) {
      ReparentWalker walk (this);
      prefix_walk (walk, &iter, true);
    }
  }

  // maybe emit the 'row has child toggled' signal
  if (!old_size && parent_row!=root) {
    GtkTreeIter parent_it (get_iter (parent_row));
    gtk_tree_path_up (path);
    gtk_tree_model_row_has_child_toggled (model, path, &parent_it);
  }

  // cleanup
  gtk_tree_path_free (path);
}

void
PanTreeStore :: reparent (Row  * new_parent,
                          Row  * row,
                          int    position)
{
  g_return_if_fail (row != nullptr);

  GtkTreeModel * model (GTK_TREE_MODEL(this));

  if (!new_parent)
    new_parent = root;

  // remove our subtree's toplevel from its old parent
  rows_t tmp;
  tmp.push_back (row);
  remove_siblings (tmp, false);

  // add the subtree's toplevel to its new parent row
  const int new_parent_old_n_children (new_parent->n_children());
  position = std::min (position, new_parent_old_n_children);
  new_parent->children.insert (new_parent->children.begin()+position, row);
  renumber_children (new_parent, position);

  // emit a row-inserted for iter
  GtkTreeIter iter (get_iter (row));
  GtkTreePath * path (get_path (row));
  gtk_tree_model_row_inserted (model, path, &iter);
  gtk_tree_path_free (path);

  // this emits all the row-inserted and has-child-toggled signals EXCEPT
  // for iter's row-inserted (above) and parent-iter's has-child-toggled (below).
  // It's kind of kludgy but gets all the signals emitted in the right sequence.
  ReparentWalker walk (this);
  prefix_walk (walk, &iter, true);

  // if this was the new parent's first child, fire a has-child-toggled event
  if (!new_parent_old_n_children) {
    GtkTreePath * path (get_path (new_parent));
    GtkTreeIter new_parent_iter;
    get_iter (new_parent, &new_parent_iter);
    gtk_tree_model_row_has_child_toggled (model, path, &new_parent_iter);
    gtk_tree_path_free (path);
  }
}

void
PanTreeStore :: reparent (const parent_to_children_t& new_parents)
{
  const RowCompareByDepth depth_compare (this);

  // REMOVE children from their OLD parents

  rows_t remove_me;
  foreach_const (parent_to_children_t, new_parents, it)
    remove_me.insert (remove_me.end(), it->second.begin(), it->second.end());
  remove (remove_me, false);

  // ADD children to their NEW parent

  insert_sorted (new_parents);
}

/***
****
***/

void
PanTreeStore :: remove (const rows_t& rows, bool delete_rows)
{
  parent_to_children_t parent_to_kids;
  foreach_const (rows_t, rows, it) {
    g_assert (*it);
    g_assert ((*it)->parent);
    parent_to_kids[(*it)->parent].push_back (*it);
  }

  // remove them in same-parent batches, starting at the
  // deepest parents and working up to the shallower ones
  rows_t keys;
  foreach_const (parent_to_children_t, parent_to_kids, it)
    keys.push_back (it->first);
  const RowCompareByDepth compare_depth (this);
  std::sort (keys.begin(), keys.end(), compare_depth);
  foreach_const_r (rows_t, keys, it)
    remove_siblings (parent_to_kids[*it], delete_rows);
}

struct PanTreeStore::ClearWalker: public PanTreeStore::WalkFunctor
{
  virtual ~ClearWalker () {}

  void clear_children (PanTreeStore* store, Row * row)
  {
    store->remove_siblings (row->children, true);
  }

  virtual bool operator()(PanTreeStore* store,
                          PanTreeStore::Row*,
                          GtkTreeIter* iter, GtkTreePath*)
  {
    clear_children (store, store->get_row(iter));
    return true;
  }
};

void
PanTreeStore :: clear ()
{
  ClearWalker walker;
  postfix_walk (walker);
  walker.clear_children (this, root);
}


struct PanTreeStore :: RowCompareByChildPos {
  bool operator() (const Row* a, const Row* b) const {
    return a->child_index < b->child_index;
  }
};

void
PanTreeStore :: remove_siblings (const rows_t& siblings,
                                 bool          delete_rows)
{
  // entry assertions
  g_assert (!siblings.empty());
  Row * parent (siblings[0]->parent);
  g_assert (parent);
  foreach_const (rows_t, siblings, it)
    g_assert ((*it)->parent == parent); // all are siblings

  // unthread the doomed rows
  std::set<int> removed_indices;
  GtkTreeModel * model (GTK_TREE_MODEL(this));
  GtkTreePath * path (get_path (parent));
  foreach_const (rows_t, siblings, nit) {
    Row * row (*nit);
    removed_indices.insert (row->child_index);
    row->parent = nullptr;
    row->child_index = -1;
  }

  // remove the dead rows; re-index the live ones
  int pos (0);
  rows_t keepers, purged;
  keepers.reserve (parent->n_children());
  foreach_const (rows_t, parent->children, it) {
    Row * row (*it);
    if (!row->parent)
      purged.push_back (row);
    else {
      row->child_index = pos++;
      keepers.push_back (row);
    }
  }
  parent->children.swap (keepers);

  // fire removal signal for child
  foreach_const_r (std::set<int>, removed_indices, it) {
    gtk_tree_path_append_index (path, *it);
    gtk_tree_model_row_deleted (model, path);
    gtk_tree_path_up (path);
  }

  // maybe fire has-children signal for parent
  if (parent!=root && !parent->n_children()) {
    GtkTreeIter pit;
    set_iter (&pit, parent);
    gtk_tree_model_row_has_child_toggled (model, path, &pit);
  }

  // clean up purged rows
  if (delete_rows) {
    foreach (rows_t, purged, it) {
      Row * row (*it);
      g_assert (row->child_index == -1);
      FreeRowWalker walk (this);
      GtkTreeIter iter (get_iter (row));
      postfix_walk (walk, &iter);
    }
  }

  gtk_tree_path_free (path);
}

/***
****
***/

bool
PanTreeStore :: walk_helper (int           walk_mode,
                             Row         * row,
                             GtkTreePath * path,
                             WalkFunctor & walker)
{
  g_assert (row);
  g_assert (walk_mode==WALK_PREFIX || walk_mode==WALK_POSTFIX);

  bool more (true);

  GtkTreeIter iter;
  set_iter (&iter, row);

  if (more && row!=root && walk_mode==WALK_PREFIX)
    more = walker (this, row, &iter, path);

  const size_t n_children (row->n_children());
  if (more && n_children) {
    if (path)
      gtk_tree_path_append_index (path, 0);
    for (Row ** it(&row->children.front()),
              ** end(it+n_children); more && it!=end; ++it) {
      more = walk_helper (walk_mode, *it, path, walker);
      if (path)
        gtk_tree_path_next (path);
    }
    if (path)
      gtk_tree_path_up (path);
  }

  if (more && row!=root && walk_mode==WALK_POSTFIX)
    more = walker (this, row, &iter, path);

  return more;
}

void
PanTreeStore :: walk (int             walk_mode,
                      WalkFunctor   & walker,
                      GtkTreeIter   * top,
                      bool            need_path)
{
  GtkTreePath * path (nullptr);
  if (need_path)
    path = top ? get_path(top) : gtk_tree_path_new();

  Row * row (top ? get_row(top) : root);
  walk_helper (walk_mode, row, path, walker);
  gtk_tree_path_free (path);
}

void
PanTreeStore :: prefix_walk (WalkFunctor   & walker,
                             GtkTreeIter   * top,
                             bool            need_path)
{
  walk (WALK_PREFIX, walker, top, need_path);
}

void
PanTreeStore :: postfix_walk (WalkFunctor   & walker,
                              GtkTreeIter   * top,
                              bool            need_path)
{
  walk (WALK_POSTFIX, walker, top, need_path);
}

/***
****
***/

PanTreeStore :: Row*
PanTreeStore :: get_prev (Row * row)
{
  if (!row || row==root)
    return nullptr;
  if (row->child_index==0)
    return row->parent==root ? nullptr : row->parent;
  Row * sibling = row->parent->nth_child (row->child_index-1);
  return sibling->get_last_descendant ();
}

PanTreeStore :: Row*
PanTreeStore :: get_next (Row * row)
{
  // child
  if (!row->children.empty())
    return row->nth_child (0);

  // sibling
  while (row && row!=root) {
    Row * sibling = row->parent->nth_child (row->child_index + 1);
    if (sibling)
      return sibling;
    row = row->parent;
  }

  // if all else fails, just return the first node
  return root->nth_child(0);
}

bool
PanTreeStore :: get_prev (GtkTreeIter * iter)
{
  return set_or_invalidate (iter, get_prev(get_row(iter)));
}

bool
PanTreeStore :: get_next (GtkTreeIter * iter)
{
  return set_or_invalidate (iter, get_next(get_row(iter)));
}

bool
PanTreeStore :: back (GtkTreeIter * iter)
{
  return set_or_invalidate (iter, root->get_last_descendant());
}

bool
PanTreeStore :: front (GtkTreeIter * iter)
{
  return set_or_invalidate (iter, get_next(root));
}
