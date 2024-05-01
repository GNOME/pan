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

#ifndef PAN_TREE_STORE_H
#define PAN_TREE_STORE_H

#include <map>
#include <vector>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#define PAN_TREE_STORE_TYPE (PanTreeStore::get_type())
#define PAN_TREE_STORE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), PAN_TREE_STORE_TYPE, PanTreeStore))
#define PAN_TREE_STORE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass),  PAN_TREE_STORE_TYPE, PanTreeStoreClass))
#define IS_PAN_TREE_STORE(obj)\
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), PAN_TREE_STORE_TYPE))
#define IS_PAN_TREE_STORE__CLASS(klass)\
    (G_TYPE_CHECK_CLASS_TYPE ((klass),  PAN_TREE_STORE_TYPE))
#define PAN_TREE_STORE_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS ((obj),  PAN_TREE_STORE_TYPE, PanTreeStoreClass))

struct PanTreeStoreClass
{
  GObjectClass parent_class;
};

/**
 * PanTreeStore is a GtkTreeModel implementation with a primary goal of
 * fast, memory-efficient handling of very large and/or flat trees.
 * It has no Pan dependencies and can be reused by copying pan-tree.{h,cc}.
 *
 * Enhancements, as compared to GtkTreeStore:
 *
 *  1. Rows are constructed by the client, then passed into the tree.
 *     This helps lower memory use by having a single Row structure
 *     rather than an array of GValues.  On an x86 architecture,
 *     sizeof(int)==4 and sizeof(GValue)==20.  (Plus we avoid
 *     GtkTreeStore's extra hit of using linked lists to join the
 *     cells in a row,
 *
 *  2. Population before insertion is also much faster because
 *     there are no extra calls to tree_store_set_value(), no GObject
 *     type checking, no extra GtkTreePaths built for row-changed, and
 *     most importantly, no row-changed signals emitted at all.
 *
 *  3. Rows can be inserted in batches, which reduces some overhead
 *     of redundant per-call GObject type checking and GtkTreePath creation.
 *     for the row-inserted signal handlers.
 *
 *  4. Rows can be inserted into the tree sorted so that there's no
 *     separate sorting step.
 *
 *  5. GtkTreePath creation is practically free compared with GtkTreeStore:
 *     each child knows its child index to be used in the tree path.
 *     GtkTreeStore holds a list of children which must be walked in order
 *     to find the child's index, which is expensive on large/flat trees.
 *
 *  6. Entire subtrees can be reparented in a single call.
 *
 *
 * WARTS
 *
 *  _  Sorting is not triggered when a row changes.
 *     When you're done with a batch of changes, call sort() manually.
 *
 *  _  Would have a better chance of being used outside of Pan if it
 *     had a C API.
 */
struct PanTreeStore
{
  public:

    // must come first for glib-style inheritance to work right.
    GObject parent;

  public:

    /** This is how you instantiate.
        Arguments are equivalent to gtk_tree_store_new(). */
    static PanTreeStore* new_tree (int n_cols, ...);

    static GType get_type ();


  private:

    class ClearWalker;
    class FreeRowWalker;
    class ReparentWalker;
    class RowCompareByDepth;
    class RowCompareByColumn;
    class RowCompareByChildPos;

  public:

    struct Row;
    typedef std::vector<Row*> rows_t;

    /**
     * PanTreeStore saves rows in structs rather than arrays
     * of GValues.  This is for flexibility (the client can define
     * the structs however they want) and for space efficiency
     * sizeof(GValue) > sizeof(builtin).
     *
     * It's the client's responsibility to call
     * store->row_changed(row) when a row's contents change.
     *
     * Subclasses must implement get_value(), typically with a
     * switch(column) statement and various calls to set_value_X().
     */
    struct Row
    {
      public:
        virtual ~Row () {}
        virtual void get_value (int column, GValue* setme) = 0;

      // helpers to make implementing get_value() easier
      protected:
        void set_value_pointer       (GValue * setme, gpointer);
        void set_value_ulong         (GValue * setme, unsigned long);
        void set_value_int           (GValue * setme, int);
        void set_value_string        (GValue * setme, const char *);
        void set_value_static_string (GValue * setme, const char *);

      protected:
        Row (): parent(nullptr), child_index(-1) {}

      private:
        friend class PanTreeStore;
        friend class ClearWalker;
        friend class FreeRowWalker;
        friend class ReparentWalker;
        friend class RowCompareByChildPos;

        Row * parent;
        rows_t children;
        int child_index;

      private:
        Row* get_last_descendant () {
          return children.empty() ? this
                                  : children.back()->get_last_descendant ();
        }
        int n_children () const {
          return (int) children.size();
        }
        Row * nth_child (int n) {
          Row * ret (nullptr);
          if (0<=n && n<(int)children.size())
            ret = children[n];
          return ret;
        }
    };

    struct RowDispose
    {
      virtual ~RowDispose () {}
      virtual void row_dispose (PanTreeStore *store, Row* row) = 0;
    };

    void set_row_dispose (RowDispose * r) { row_dispose = r; }

    /** Client code _must_ call row_changed() when a row changes. */
    void row_changed (GtkTreeIter * iter);
    /** Client code _must_ call row_changed() when a row changes. */
    void row_changed (Row * row);

    void get_iter (const Row*, GtkTreeIter* setme);
    GtkTreeIter get_iter (const Row*);

    /** Get the Row pointed to by the GtkTreeIter. */
    Row* get_row (GtkTreeIter* iter);

    /** Get the Row pointed to by the GtkTreeIter. */
    const Row* get_row (const GtkTreeIter* iter) const;

    /** Build a GtkTreePath corresponding to the GtkTreeIter.
        This is equivalent to gtk_tree_model_get_path(iter). */
    GtkTreePath* get_path (GtkTreeIter* iter);

    /** Build a GtkTreePath corresponding to the Row */
    GtkTreePath* get_path (const Row* row) const;

    bool back (GtkTreeIter * iter);
    bool front (GtkTreeIter * iter);
    bool get_next (GtkTreeIter * iter);
    bool get_prev (GtkTreeIter * iter);
    Row*  get_next (Row * iter);
    Row*  get_prev (Row * iter);

  public:

    typedef std::map<Row*,rows_t> parent_to_children_t;

    void insert_sorted (const parent_to_children_t&);

    void insert_sorted (Row * parent, const rows_t& children);

    void insert (Row * parent, const rows_t& children, int pos);

    void append (Row * parent, const rows_t& children)
      { insert (parent, children, INT_MAX); }

    void append (Row * parent, Row * child)
      { rows_t children; children.push_back(child); append (parent, children); }

    /** Empties out the tree. */
    void clear ();

    /** Remove a set of rows.
        These do not need to have the same parent. */
    void remove   (const rows_t& rows) { remove (rows, true ); }

    /** Move a row to a new parent. */
    void reparent (Row            * parent_or_null_for_root,
                   Row            * child,
                   int              pos = INT_MAX);

    void reparent (const parent_to_children_t& parents_to_children);

  public:

    void pause_sorting () { ++sort_paused; }
    void resume_sorting () { if (!--sort_paused) sort (); }

  public:

    /** Clients wanting to walk a tree or subtree should
        subclass this and pass an instantiation to walk(). */
    struct WalkFunctor {
      virtual ~WalkFunctor () {}
      virtual bool operator()(PanTreeStore*, Row*, GtkTreeIter*, GtkTreePath*) = 0;
    };

    /**
     * The default walk, executing a parent-first traversal of the entire tree,
     * with GtkTreePaths not generated (they can be turned on if needed, but add
     * extra expense so are turned off by default).
     *
     * For more flexibility, use prefix_walk() or postfix_walk().
     */
    void walk (WalkFunctor& walk, bool need_path=false) { prefix_walk (walk, nullptr, need_path); }

    void prefix_walk (WalkFunctor   & walk_functor,
                      GtkTreeIter   * top = nullptr,
                      bool            need_path = false);

    void postfix_walk (WalkFunctor   & walk_functor,
                       GtkTreeIter   * top = nullptr,
                       bool            need_path = false);

  public:

    void get_sort_column_id (int& column, GtkSortType& type) const {
      column = sort_column_id;
      type = order;
    }

  public:

    bool is_sorted () const { return sort_info->count (sort_column_id); }

    bool is_root (const GtkTreeIter * it) const;

    size_t get_depth (const Row * row) const;

    bool is_in_tree (Row * row) const;

    /** Equivalent to gtk_tree_model_get_parent(). */
    bool get_parent (GtkTreeIter * setme_parent,
                     GtkTreeIter * child);


  /****
  *****  EVERYTHING PAST THIS POINT IS PRIVATE
  ****/

  private:

    enum { SORT, FLIP };
    void sort (int mode=SORT);

    /**
     * Per-row sorting information, as set by
     * gtk_tree_sortable_set_sort_func().
     */
    struct SortInfo
    {
      GtkTreeIterCompareFunc sort_func;
      gpointer user_data;
      GDestroyNotify destroy_notify;

      SortInfo(): sort_func(nullptr), user_data(nullptr), destroy_notify(nullptr) {}
      ~SortInfo() { clear(); }

      void clear () {
        if (destroy_notify) destroy_notify (user_data);
        sort_func = nullptr;
        user_data = nullptr;
        destroy_notify = nullptr;
      }

      void assign (GtkTreeIterCompareFunc sort_func,
                   gpointer user_data,
                   GDestroyNotify destroy_notify) {
        clear ();
        this->sort_func = sort_func;
        this->user_data = user_data;
        this->destroy_notify = destroy_notify;
      }
    };

    /** sort info for any column */
    typedef std::map<int,SortInfo> column_sort_info_t;
    column_sort_info_t * sort_info;

    /** one of GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
               GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,
               or [0...n_columns) */
    int sort_column_id;

    /** the model's sort order -- ascending or descending */
    GtkSortType order;

  private:

    void set_iter (GtkTreeIter*, const Row*);

    static void invalidate_iter (GtkTreeIter*);

    bool set_or_invalidate (GtkTreeIter*, const Row*);

  private:

    void remove (const rows_t& rows, bool delete_rows);

    void remove_siblings (const rows_t& siblings, bool delete_rows);

    void renumber_children (Row * parent,
                            int child_lo = 0,
                            int child_hi = INT_MAX);

  private:

    enum { WALK_PREFIX, WALK_POSTFIX };

    void walk (int             walk_mode,
               WalkFunctor   & walker,
               GtkTreeIter   * top,
               bool            need_path);

    bool walk_helper (int             walk_mode,
                      Row           * top_row,
                      GtkTreePath   * top_path_or_null,
                      WalkFunctor   & walk_functor);
  private:

    /** Used just as in GtkTreeStore, to verify that GtkIters
        passed as arguments to us actually do belong to this tree. */
    int stamp;

    /** Defines the GType of each column. */
    std::vector<GType> * column_types;

    /** number of columns in each row. */
    int n_columns;

    /** is sorting currently disabled? */
    int sort_paused;

    /** The root node,  This is for implementation only;
        it's invisible to the outside world and you can't
        get a GtkTreeIter or GtkTreePath to it. */
    Row * root;

    /** How to dispose of a row that's no longer used.
        If this is null, we just call `delete row;' */
    RowDispose * row_dispose;


  private: // gobject
    static void pan_tree_dispose (GObject *);
    static void pan_tree_finalize (GObject *);
    static void pan_tree_class_init (PanTreeStoreClass *);
    static void pan_tree_model_init (GtkTreeModelIface *);
    static void pan_tree_sortable_init (GtkTreeSortableIface *);
    static void pan_tree_init (PanTreeStore *);

  private: // GtkTreeModel implementation
    static GtkTreeModelFlags model_get_flags  (GtkTreeModel*);
    static gint         model_get_n_columns   (GtkTreeModel*);
    static GType        model_get_column_type (GtkTreeModel*, int);
    static gboolean     model_get_iter        (GtkTreeModel*,
                                               GtkTreeIter*,
                                               GtkTreePath*);
    static GtkTreePath* model_get_path        (GtkTreeModel*, GtkTreeIter*);
    static void         model_get_value       (GtkTreeModel*, GtkTreeIter*,
                                               gint, GValue*);
    static gboolean     model_iter_next       (GtkTreeModel*, GtkTreeIter*);
    static gboolean     model_iter_children   (GtkTreeModel*, GtkTreeIter*,
                                               GtkTreeIter*);
    static gint         model_iter_n_children (GtkTreeModel*, GtkTreeIter*);
    static gboolean     model_iter_has_child  (GtkTreeModel*, GtkTreeIter*);
    static gboolean     model_iter_nth_child  (GtkTreeModel*, GtkTreeIter*,
                                               GtkTreeIter*, gint);
    static gboolean     model_iter_parent     (GtkTreeModel*, GtkTreeIter*,
                                               GtkTreeIter*);

  private: // GtkTreeSortable implementation
    static gboolean sortable_get_sort_column_id    (GtkTreeSortable*,
                                                    gint*,
                                                    GtkSortType*);
    static void     sortable_set_sort_column_id    (GtkTreeSortable*,
                                                    gint,
                                                    GtkSortType);
    static void     sortable_set_sort_func         (GtkTreeSortable*,
                                                    gint,
                                                    GtkTreeIterCompareFunc,
                                                    gpointer,
                                                    GDestroyNotify);
    static gboolean sortable_has_sort_func         (GtkTreeSortable*,
                                                    gint);
    static void     sortable_set_default_sort_func (GtkTreeSortable*,
                                                    GtkTreeIterCompareFunc,
                                                    gpointer,
                                                    GDestroyNotify);
    static gboolean sortable_has_default_sort_func (GtkTreeSortable*);

  private:
    struct SortData;
    struct SortRowInfo;
    void sort_children (SortInfo&, Row* parent, bool recurse, int mode);
    static int row_compare_func (gconstpointer, gconstpointer, gpointer);

  private:
    PanTreeStore(); // this is a GObject; use new_tree instead
};

#endif
