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

extern "C" {
  #include <config.h>
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
  #include <gdk/gdkkeysyms.h>
}
#include <cctype>
#include <cmath>
#include <algorithm>
#include <pan/general/debug.h>
//#include <pan/general/gdk-threads.h>
#include <pan/general/e-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/rules-info.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/icons/pan-pixbufs.h>
#include "header-pane.h"
#include "render-bytes.h"
#include "tango-colors.h"
#include "gtk_compat.h"

using namespace pan;

/****
*****
****/

namespace
{
  enum
  {
    COL_DATE_STR, COL_STATE, COL_ACTION, COL_SCORE,
    COL_LINES, COL_BYTES, COL_DATE, COL_ARTICLE_POINTER,
    COL_SUBJECT, COL_SHORT_AUTHOR, N_COLUMNS
  };
}

const Article*
HeaderPane :: get_article (GtkTreeModel* model, GtkTreeIter* iter)
{
  const Article * a = dynamic_cast<Row*>(PAN_TREE_STORE(model)->get_row(iter))->article;
  g_assert (a != 0);
  return a;
}

/****
*****
****/

namespace
{
  typedef std::set<const Article*> articles_t;

  enum
  {
    ICON_READ,
    ICON_UNREAD,

    ICON_COMPLETE,
    ICON_COMPLETE_READ,

    ICON_INCOMPLETE,
    ICON_INCOMPLETE_READ,

    ICON_CACHED,
    ICON_QUEUED,
    ICON_ERROR,
    ICON_EMPTY,
    ICON_FLAGGED,
    ICON_QTY
   };

  struct Icon {
    const guint8 * pixbuf_txt;
    GdkPixbuf * pixbuf;
  } _icons[ICON_QTY] = {
    { icon_article_read,           0 },
    { icon_article_unread,         0 },

    { icon_binary_complete,        0 },
    { icon_binary_complete_read,   0 },

    { icon_binary_incomplete,      0 },
    { icon_binary_incomplete_read, 0 },

    { icon_disk,                   0 },
    { icon_bluecheck,              0 },
    { icon_x,                      0 },
    { icon_empty,                  0 },
    { icon_red_flag,               0 }
  };

  int
  get_article_state (const Data& data, const Article * a)
  {
    int retval;
    const bool read (data.is_read (a));
    const int part_state (a->get_part_state());

    if (part_state==Article::COMPLETE && read)
      retval = ICON_COMPLETE_READ;
    else if (part_state==Article::COMPLETE)
      retval = ICON_COMPLETE;
    else if (part_state==Article::INCOMPLETE && read)
      retval = ICON_INCOMPLETE_READ;
    else if (part_state==Article::INCOMPLETE)
      retval = ICON_INCOMPLETE;
    else if (read)
      retval = ICON_READ;
    else
      retval = ICON_UNREAD;

    return retval;
  }

  int
  get_article_action (const Article       * article,
                      const ArticleCache  & cache,
                      const Queue         & queue,
                      const Quark         & message_id)
  {
    int offset (ICON_EMPTY);

    if (queue.contains (message_id))
      offset = ICON_QUEUED;
    else if (cache.contains (message_id))
      offset = ICON_CACHED;

    if (article)
      if (article->flag)
        offset = ICON_FLAGGED;

    return offset;
  }
};


void
HeaderPane :: render_action (GtkTreeViewColumn * ,
                             GtkCellRenderer   * renderer,
                             GtkTreeModel      * model,
                             GtkTreeIter       * iter,
                             gpointer            )
{
  int index (0);
  gtk_tree_model_get (model, iter, COL_ACTION, &index, -1);
  g_object_set (renderer, "pixbuf", _icons[index].pixbuf, NULL);
}

void
HeaderPane :: render_state (GtkTreeViewColumn * ,
                            GtkCellRenderer   * renderer,
                            GtkTreeModel      * model,
                            GtkTreeIter       * iter,
                            gpointer            )
{
  int index (0);
  gtk_tree_model_get (model, iter, COL_STATE, &index, -1);
  g_object_set (renderer, "pixbuf", _icons[index].pixbuf, NULL);
}

int
HeaderPane :: find_highest_followup_score (GtkTreeModel * model,
                                           GtkTreeIter  * parent) const
{
  int score (-9999);
  GtkTreeIter child;
  if (gtk_tree_model_iter_children (model, &child, parent)) do {
    const Article * a (get_article (model, &child));
    score = std::max (score, a->score);
    score = std::max (score, find_highest_followup_score (model, &child));
  } while (gtk_tree_model_iter_next (model, &child));
  return score;
}

void
HeaderPane :: render_score (GtkTreeViewColumn * ,
                            GtkCellRenderer   * renderer,
                            GtkTreeModel      * model,
                            GtkTreeIter       * iter,
                            gpointer            user_data)
{
  int score (0);
  gtk_tree_model_get (model, iter, COL_SCORE, &score, -1);

  char buf[16];
  if (score)
    g_snprintf(buf, sizeof(buf), "%d", score);
  else
    *buf = '\0';

  HeaderPane * self (static_cast<HeaderPane*>(user_data));
  GtkTreeView * view (GTK_TREE_VIEW (self->_tree_view));
  GtkTreePath * path (gtk_tree_model_get_path (model, iter));
  const bool expanded (gtk_tree_view_row_expanded (view, path));
  gtk_tree_path_free (path);
  if (!expanded)
    score = std::max (score, self->find_highest_followup_score (model, iter));

  const Prefs& prefs (self->_prefs);
  std::string bg, fg;
  if (score >= 9999) {
    fg = prefs.get_color_str ("score-color-watched-fg", "black");
    bg = prefs.get_color_str ("score-color-watched-bg", TANGO_CHAMELEON_LIGHT);
  } else if (score >= 5000) {
    fg = prefs.get_color_str ("score-color-high-fg", "black");
    bg = prefs.get_color_str ("score-color-high-bg", TANGO_BUTTER_LIGHT);
  } else if (score >= 1) {
    fg = prefs.get_color_str ("score-color-medium-fg", "black");
    bg = prefs.get_color_str ("score-color-medium-bg", TANGO_SKY_BLUE_LIGHT);
  } else if (score <= -9999) {
    fg = prefs.get_color_str ("score-color-ignored-fg", TANGO_ALUMINUM_4);
    bg = prefs.get_color_str ("score-color-ignored-bg", "black");
  } else if (score <= -1) {
    fg = prefs.get_color_str ("score-color-low-fg", TANGO_ALUMINUM_2);
    bg = prefs.get_color_str ("score-color-low-bg", "black");
  }

  g_object_set (renderer, "text", buf,
                          "background", (bg.empty() ? NULL : bg.c_str()),
                          "foreground", (fg.empty() ? NULL : fg.c_str()),
                          NULL);
}

void
HeaderPane :: render_bytes (GtkTreeViewColumn * ,
                            GtkCellRenderer   * renderer,
                            GtkTreeModel      * model,
                            GtkTreeIter       * iter,
                            gpointer            )
{
  unsigned long bytes (0);
  gtk_tree_model_get (model, iter, COL_BYTES, &bytes, -1);
  g_object_set (renderer, "text", pan::render_bytes(bytes), NULL);
}

struct HeaderPane::CountUnread: public PanTreeStore::WalkFunctor
{
  const Row * top_row;
  unsigned long unread_children;

  CountUnread (const Row* top): top_row(top), unread_children(0) {}
  virtual ~CountUnread () {}

  virtual bool operator()(PanTreeStore*, PanTreeStore::Row* r,
                          GtkTreeIter*, GtkTreePath* ) {
    const Row * row (dynamic_cast<Row*>(r));
    if (row!=top_row && !row->is_read)
      ++unread_children;
    return true;
  }
};


void
HeaderPane :: render_subject (GtkTreeViewColumn * ,
                              GtkCellRenderer   * renderer,
                              GtkTreeModel      * model,
                              GtkTreeIter       * iter,
                              gpointer            user_data)
{
  const HeaderPane * self (static_cast<HeaderPane*>(user_data));
  const Prefs & p(self->_prefs);
  const Row * row (dynamic_cast<Row*>(self->_tree_store->get_row (iter)));

  CountUnread counter (row);
  self->_tree_store->prefix_walk (counter, iter);

  const bool bold (!row->is_read);

  const Article * a (self->get_article (model, iter));
  const char * text (a->subject.c_str());
  char buf[512];

  bool unread (false);

  if (counter.unread_children)
  {
    // find out if the row is expanded or not
    GtkTreeView * view (GTK_TREE_VIEW (self->_tree_view));
    GtkTreePath * path (gtk_tree_model_get_path (model, iter));
    const bool expanded (gtk_tree_view_row_expanded (view, path));
    gtk_tree_path_free (path);

    if (!expanded) {
      unread = row->is_read;
      snprintf (buf, sizeof(buf), "%s (%lu)", text, counter.unread_children);
      text = buf;
    }
  }

  std::string def_bg, def_fg;
  def_fg = p.get_color_str_wo_fallback("color-read-fg");
  def_bg = p.get_color_str_wo_fallback("color-read-bg");

  g_object_set (renderer,
    "text", text,
    "weight", (bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL),
    "foreground", unread ? (def_fg.empty() ? NULL : def_fg.c_str()) : NULL,
    "background", unread ? (def_bg.empty() ? NULL : def_bg.c_str()) : NULL, NULL);

}

HeaderPane::Row*
HeaderPane :: get_row (const Quark& message_id)
{
  mid_to_row_t::iterator it (_mid_to_row.find (message_id));
  return it==_mid_to_row.end() ? 0 : *it;
}

HeaderPane::Row*
HeaderPane :: create_row (const EvolutionDateMaker & e,
                          const Article            * a)
{
  const int action (get_article_action (a, _cache, _queue, a->message_id));
  const int state (get_article_state (_data, a));
  char * date_str (e.get_date_string (a->time_posted));
  Row * row = new Row (_data, a, date_str, action, state);

  std::pair<mid_to_row_t::iterator,bool> result (_mid_to_row.insert (row));
  g_assert (result.second);

  return row;
}

namespace
{
  typedef std::vector<const Article*> article_v;
}

void
HeaderPane ::  add_children_to_model (PanTreeStore               * store,
                                      PanTreeStore::Row          * parent_row,
                                      const Quark                & parent_mid,
                                      const Data::ArticleTree    * atree,
                                      const EvolutionDateMaker   & date_maker,
                                      const bool                   do_thread)
{
  // see if this parent has any children...
  article_v children;
  atree->get_children (parent_mid, children);
  if (children.empty())
    return;

  // add all these children...
  PanTreeStore::rows_t rows;
  rows.reserve (children.size());
  foreach_const (article_v, children, it)
    rows.push_back (create_row (date_maker, *it));
  store->append (do_thread ? parent_row : 0, rows);

  // recurse
  for (size_t i=0, n=children.size(); i<n; ++i)
    add_children_to_model (store, rows[i], children[i]->message_id, atree, date_maker, do_thread);
}

int
HeaderPane :: column_compare_func (GtkTreeModel  * model,
                                   GtkTreeIter   * iter_a,
                                   GtkTreeIter   * iter_b,
                                   gpointer        )
{
  int ret (0);
  const PanTreeStore * store (reinterpret_cast<PanTreeStore*>(model));
  const Row& row_a (*static_cast<const Row*>(store->get_row (iter_a)));
  const Row& row_b (*static_cast<const Row*>(store->get_row (iter_b)));

  int sortcol;
  GtkSortType order;
  store->get_sort_column_id (sortcol, order);

  const bool is_root (store->is_root (iter_a));
  if (!is_root)
    sortcol = COL_DATE;

  switch (sortcol)
  {
    case COL_STATE:
      ret = row_a.state - row_b.state;
      break;

    case COL_ACTION:
      //const int a_action (store->get_cell_int (iter_a, COL_ACTION));
      //const int b_action (store->get_cell_int (iter_b, COL_ACTION));
      //ret = a_action - b_action;
      break;

    case COL_SUBJECT:
      ret = strcmp (row_a.get_collated_subject (),
                    row_b.get_collated_subject ());
      break;

    case COL_SCORE:
      ret = row_a.article->score - row_b.article->score;
      break;

    case COL_BYTES: {
      const unsigned long a_bytes (row_a.article->get_byte_count());
      const unsigned long b_bytes (row_b.article->get_byte_count());
           if (a_bytes < b_bytes) ret = -1;
      else if (a_bytes > b_bytes) ret = 1;
      else                        ret = 0;
      break;
    }

    case COL_LINES: {
      const unsigned long a_lines (row_a.article->get_line_count());
      const unsigned long b_lines (row_b.article->get_line_count());
           if (a_lines < b_lines) ret = -1;
      else if (a_lines > b_lines) ret = 1;
      else                        ret = 0;
      break;
    }

    case COL_SHORT_AUTHOR:
      ret = strcmp (row_a.get_collated_author (), row_b.get_collated_author ());
      break;

    default: { // COL_DATE
      const time_t a_time (row_a.article->time_posted);
      const time_t b_time (row_b.article->time_posted);
           if (a_time < b_time) ret = -1;
      else if (a_time > b_time) ret = 1;
      else                      ret = 0;
      break;
    }
  }

  // we _always_ want the lower levels to be sorted by date
  if (!is_root && order==GTK_SORT_DESCENDING)
    ret = -ret;

  return ret;
}

PanTreeStore*
HeaderPane :: build_model (const Quark               & group,
                           Data::ArticleTree         * atree,
                           const TextMatch           * )
{
  PanTreeStore * store = PanTreeStore :: new_tree (
    N_COLUMNS,
    G_TYPE_STRING,  // date string
    G_TYPE_INT,     // state
    G_TYPE_INT,     // action
    G_TYPE_INT,     // score
    G_TYPE_ULONG,   // lines
    G_TYPE_ULONG,   // bytes
    G_TYPE_ULONG,   //  date
    G_TYPE_POINTER, // article pointer
    G_TYPE_STRING,  // subject
    G_TYPE_STRING); // short author

  GtkTreeSortable * sort = GTK_TREE_SORTABLE(store);
  for (int i=0; i<N_COLUMNS; ++i)
    gtk_tree_sortable_set_sort_func (sort, i, column_compare_func, GINT_TO_POINTER(i), NULL);
  if (!group.empty()) {
    const EvolutionDateMaker date_maker;
    const bool do_thread (_prefs.get_flag ("thread-headers", true));
    add_children_to_model (store, NULL, Quark(), atree, date_maker, do_thread);
  }

  return store;
}

namespace
{
  void save_sort_order (Prefs& prefs, PanTreeStore* store)
  {
    g_assert (store);
    g_assert (GTK_IS_TREE_SORTABLE(store));

    gint sort_column (0);
    GtkSortType sort_type;
    gtk_tree_sortable_get_sort_column_id (GTK_TREE_SORTABLE(store), &sort_column,  &sort_type);
    prefs.set_int ("header-pane-sort-column", sort_column);
    prefs.set_flag ("header-pane-sort-ascending", sort_type==GTK_SORT_ASCENDING);
  }
}

void
HeaderPane :: rebuild ()
{
  quarks_t selectme;
  if (1) {
    const articles_set old_selection (get_full_selection ());
    foreach_const (articles_set, old_selection, it)
      selectme.insert ((*it)->message_id);
  }

  _mid_to_row.clear ();
  _tree_store = build_model (_group, _atree, NULL);

  const bool sort_ascending = _prefs.get_flag ("header-pane-sort-ascending", false);
  int sort_column = _prefs.get_int ("header-pane-sort-column", COL_DATE);
  if (sort_column<0 || sort_column>=N_COLUMNS) // safeguard against odd settings
    sort_column = COL_DATE;
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE(_tree_store),
                                        sort_column,
                                        sort_ascending ? GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);

  GtkTreeModel * model (GTK_TREE_MODEL (_tree_store));
  GtkTreeView * view (GTK_TREE_VIEW (_tree_view));
  gtk_tree_view_set_model (view, model);
  g_object_unref (G_OBJECT(_tree_store)); // store is deleted w/view

  if (_prefs.get_flag ("expand-threads-when-entering-group", false))
    gtk_tree_view_expand_all (view);

  if (!selectme.empty())
    select_message_id (*selectme.begin());
}

bool
HeaderPane :: set_group (const Quark& new_group)
{
  const Quark old_group (_group);
  bool change (old_group != new_group);

  if (change)
  {
    if (!_group.empty() && _prefs.get_flag ("mark-group-read-when-leaving-group", false))
      _data.mark_group_read (_group);

    if (_atree)
        _atree->remove_listener (this);

    if (_tree_store)
    {
      save_sort_order (_prefs, _tree_store);
      _mid_to_row.clear ();
      _tree_store = 0;
      gtk_tree_view_set_model (GTK_TREE_VIEW(_tree_view), NULL);
    }

    _group = new_group;

    delete _atree;
    _atree = 0;

    char * pch = g_build_filename (g_get_home_dir(), "News", NULL);
    Quark path(_group_prefs.get_string (_group, "default-group-save-path", pch));
    g_free(pch);

    if (!_group.empty())
    {
      _atree = _data.group_get_articles (new_group, path, _show_type, &_filter,&_rules,&_queue);
      _atree->add_listener (this);

      rebuild ();
      if (gtk_widget_get_realized(_tree_view))
        gtk_tree_view_scroll_to_point (GTK_TREE_VIEW(_tree_view), 0, 0);
    }
  }

  return change;
}

/****
*****
****/

void
HeaderPane :: rebuild_article_state (const Quark& message_id)
{
  Row * row (get_row (message_id));
  const Article * article (row->article);
  const int is_read (_data.is_read (article));
  const int state (get_article_state (_data, article));
  const bool changed ((state!=row->state) || (is_read!=row->is_read));
  row->state = state;
  row->is_read = is_read;
  if (changed)
    _tree_store->row_changed (row);
}

void
HeaderPane :: rebuild_all_article_states ()
{
  foreach (mid_to_row_t, _mid_to_row, it)
    rebuild_article_state ((*it)->article->message_id);
}

void
HeaderPane :: on_group_read (const Quark& group)
{
  if (group == _group)
  {
    rebuild_all_article_states ();
    gtk_widget_queue_draw (_tree_view);
  }
}

/****
*****
****/

namespace
{
  struct ArticleIsNotInSet: public ArticleTester
  {
    const quarks_t& mids;
    ArticleIsNotInSet (const quarks_t& m): mids(m) {}
    virtual ~ArticleIsNotInSet () {}
    virtual bool operator()(const Article& a) const {
      return !mids.count(a.message_id);
    }
  };

  struct RememberMessageId: public RowActionFunctor
  {
    quarks_t& mids;
    RememberMessageId (quarks_t& m): mids(m) {}
    virtual ~RememberMessageId() {}
    virtual void operator() (GtkTreeModel*, GtkTreeIter*, const Article& article) {
      mids.insert (article.message_id);
    }
  };
}

void
HeaderPane :: select_message_id (const Quark& mid, bool do_scroll)
{
  HeaderPane::Row * row = get_row (mid);
  GtkTreePath * path (_tree_store->get_path(row));
  GtkTreeView * view (GTK_TREE_VIEW(_tree_view));
  const bool expand (_prefs.get_flag("expand-selected-articles",false));
  if (expand) gtk_tree_view_expand_to_path (view, path);
  GtkTreeSelection * sel (gtk_tree_view_get_selection (view));
  gtk_tree_selection_select_path (sel, path);
  if (do_scroll) {
    gtk_tree_view_set_cursor (view, path, NULL, false);
    gtk_tree_view_scroll_to_cell (view, path, NULL, true, 0.5f, 0.0f);
  }
  gtk_tree_path_free (path);
}

void
HeaderPane :: on_tree_change (const Data::ArticleTree::Diffs& diffs)
{
  debug (diffs.added.size() << " article added; "
      << diffs.reparented.size() << " reparented; "
      << diffs.removed.size() << " removed");

  // we might need to change the selection after the update.
  const article_v old_selection (get_full_selection_v ());
  quarks_t new_selection;
  foreach_const (article_v, old_selection, it)
    if (!diffs.removed.count ((*it)->message_id))
        new_selection.insert ((*it)->message_id);

  // if the old selection survived,
  // is it visible on the screen?
  bool selection_was_visible (true);
  if (!new_selection.empty()) {
    GtkTreeView *view (GTK_TREE_VIEW(_tree_view));
    Row * row (get_row (*new_selection.begin()));
    GtkTreePath *a(0), *b(0), *p (_tree_store->get_path (row));
    gtk_tree_view_get_visible_range (view, &a, &b);
    selection_was_visible = (gtk_tree_path_compare(a,p)<=0 &&
                             gtk_tree_path_compare(p,b)<=0);
    gtk_tree_path_free (a);
    gtk_tree_path_free (b);
    gtk_tree_path_free (p);
  }

  // if none of the current selection survived,
  // we need to select something to replace the
  // current selection.
  if (!old_selection.empty() && new_selection.empty()) {
    ArticleIsNotInSet tester (diffs.removed);
    RememberMessageId actor (new_selection);
    action_next_if (tester, actor);
  }

  // added...
  const bool do_thread (_prefs.get_flag ("thread-headers", true));
  if (!diffs.added.empty()) {
    const EvolutionDateMaker date_maker;
    PanTreeStore::parent_to_children_t tmp;
    foreach_const (Data::ArticleTree::Diffs::added_t, diffs.added, it)
      create_row (date_maker, _atree->get_article(it->first));
    foreach_const (Data::ArticleTree::Diffs::added_t, diffs.added, it) {
      Row * parent (do_thread ? get_row (it->second.parent) : 0);
      Row * child (get_row (it->first));
      tmp[parent].push_back (child);
    }

    g_object_ref(G_OBJECT (_tree_store));
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), NULL);
    _tree_store->insert_sorted (tmp);
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), GTK_TREE_MODEL (_tree_store));
    g_object_unref(G_OBJECT(_tree_store));
  }

  // reparent...
  if (do_thread && !diffs.reparented.empty()) {
    PanTreeStore::parent_to_children_t tmp;
    foreach_const (Data::ArticleTree::Diffs::reparented_t, diffs.reparented, it) {
      Row * parent (get_row (it->second.new_parent));
      Row * child  (get_row (it->second.message_id));
      tmp[parent].push_back (child);
    }
    _tree_store->reparent (tmp);
  }

  // removed...
  if (!diffs.removed.empty()) {
    RowLessThan o;
    std::vector<Row*> keep;
    PanTreeStore::rows_t kill;
    std::set_difference (_mid_to_row.begin(), _mid_to_row.end(),
                         diffs.removed.begin(), diffs.removed.end(),
                         inserter (keep, keep.begin()), o);
    std::set_difference (_mid_to_row.begin(), _mid_to_row.end(),
                         keep.begin(), keep.end(),
                         inserter (kill, kill.begin()), o);
    g_assert (keep.size() + kill.size() == _mid_to_row.size());

    g_object_ref(G_OBJECT (_tree_store));
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), NULL);
    _tree_store->remove (kill);
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), GTK_TREE_MODEL (_tree_store));
    g_object_unref(G_OBJECT(_tree_store));
    _mid_to_row.get_container().swap (keep);
  }

  // changed...
  foreach_const (quarks_t, diffs.changed, it)
    rebuild_article_state (*it);

  if (!diffs.added.empty() && _prefs.get_flag ("expand-threads-when-entering-group", false))
    gtk_tree_view_expand_all (GTK_TREE_VIEW(_tree_view));

  // update our selection if necessary.
  // if the new selection has just been added or reparented,
  // and it was visible on the screen before,
  // then scroll to ensure it's still visible.
  if (!new_selection.empty()) {
    const bool do_scroll = selection_was_visible
            && (!diffs.added.empty() || !diffs.reparented.empty() || !diffs.removed.empty());
    select_message_id (*new_selection.begin(), do_scroll);
  }
}

/****
*****  SELECTION
****/

const Article*
HeaderPane :: get_first_selected_article () const
{
   const Article * a (0);
   const std::set<const Article*> articles (get_full_selection ());
   if (!articles.empty())
     a = *articles.begin ();
   return a;
}

Article*
HeaderPane :: get_first_selected_article ()
{
   Article * a (0);
   std::set<const Article*> articles (get_full_selection ());
   if (!articles.empty())
     a = (Article*)*articles.begin ();
   return a;
}

const guint
HeaderPane :: get_full_selection_rows_num() const
{
  return (gtk_tree_selection_count_selected_rows(gtk_tree_view_get_selection(GTK_TREE_VIEW(_tree_view))));
}


void
HeaderPane :: get_full_selection_v_foreach (GtkTreeModel * model,
                                            GtkTreePath  * ,
                                            GtkTreeIter  * iter,
                                            gpointer       data)
{
  static_cast<article_v*>(data)->push_back (get_article (model, iter));
}

std::vector<const Article*>
HeaderPane :: get_full_selection_v () const
{
  std::vector<const Article*> articles;
  GtkTreeView * view (GTK_TREE_VIEW (_tree_view));
  gtk_tree_selection_selected_foreach (gtk_tree_view_get_selection (view),
                                       get_full_selection_v_foreach,
                                       &articles);
  return articles;
}

std::set<const Article*>
HeaderPane :: get_full_selection () const
{
  std::set<const Article*> articles;

  const std::vector<const Article*> articles_v (get_full_selection_v());
  foreach_const (std::vector<const Article*>, articles_v, it)
    articles.insert (*it);

  return articles;
}

void
HeaderPane :: mark_all_flagged ()
{

  GtkTreeIter iter;
  GtkTreeModel * model(gtk_tree_view_get_model(GTK_TREE_VIEW(_tree_view)));
  GtkTreeSelection * sel (gtk_tree_view_get_selection (GTK_TREE_VIEW(_tree_view)));
  gtk_tree_selection_unselect_all(sel);
  gtk_tree_model_get_iter_first (model, &iter);
  walk_and_collect_flagged(model, &iter, sel);

}

void
HeaderPane :: walk_and_collect_flagged (GtkTreeModel  * model,
                                GtkTreeIter           * cur,
                                GtkTreeSelection      * setme) const
{
   for (;;) {
    const Article * a(get_article (model, cur));
    if (a->get_flag()) gtk_tree_selection_select_iter(setme,cur);
    GtkTreeIter child;
    if (gtk_tree_model_iter_children (model, &child, cur))
      walk_and_collect_flagged (model, &child, setme);
    if (!gtk_tree_model_iter_next (model, cur))
      break;
  }

}

void
HeaderPane :: walk_and_collect (GtkTreeModel          * model,
                                GtkTreeIter           * cur,
                                articles_set          & setme) const
{
  for (;;) {
    setme.insert (get_article (model, cur));
    GtkTreeIter child;
    if (gtk_tree_model_iter_children (model, &child, cur))
      walk_and_collect (model, &child, setme);
    if (!gtk_tree_model_iter_next (model, cur))
      break;
  }
}

namespace {
  struct NestedData {
    const HeaderPane * pane;
    articles_t articles;
    bool mark_all; /* for mark_article_(un)read and mark_thread_(un)read */
  };
}

void
HeaderPane :: get_nested_foreach (GtkTreeModel  * model,
                                  GtkTreePath   * path,
                                  GtkTreeIter   * iter,
                                  gpointer        data) const
{
  NestedData& ndata (*static_cast<NestedData*>(data));
  articles_set& articles (ndata.articles);
  articles.insert (get_article (model, iter));
  const bool expanded (gtk_tree_view_row_expanded (GTK_TREE_VIEW(_tree_view), path));
  GtkTreeIter child;
  if ((!expanded || ndata.mark_all) && gtk_tree_model_iter_children(model,&child,iter))
    walk_and_collect (model, &child, articles);
}

void
HeaderPane :: get_nested_foreach_static (GtkTreeModel* model,
                                         GtkTreePath* path,
                                         GtkTreeIter* iter,
                                         gpointer data)
{
  NestedData& ndata (*static_cast<NestedData*>(data));
  ndata.pane->get_nested_foreach (model, path, iter, &ndata);
}

articles_set
HeaderPane :: get_nested_selection (bool do_mark_all) const
{
  NestedData data;
  data.pane = this;
  data.mark_all = do_mark_all;
  GtkTreeSelection * selection (gtk_tree_view_get_selection (GTK_TREE_VIEW(_tree_view)));
  gtk_tree_selection_selected_foreach (selection, get_nested_foreach_static, &data);
  return data.articles;
}

/****
*****  POPUP MENU
****/

void
HeaderPane ::  do_popup_menu (GtkWidget*, GdkEventButton *event, gpointer pane_g)
{
  HeaderPane * self (static_cast<HeaderPane*>(pane_g));
  GtkWidget * menu (self->_action_manager.get_action_widget ("/header-pane-popup"));
  gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
                  (event ? event->button : 0),
                  (event ? event->time : 0));
}

namespace
{
  gboolean on_popup_menu (GtkWidget * treeview, gpointer userdata)
  {
    HeaderPane::do_popup_menu (treeview, NULL, userdata);
    return true;
  }
}
namespace
{
  bool row_collapsed_or_expanded (false);

  void row_collapsed_cb (GtkTreeView *, GtkTreeIter *,
                         GtkTreePath *, gpointer )
  {
    row_collapsed_or_expanded = true;
  }

  void row_expanded_cb (GtkTreeView *view, GtkTreeIter *,
                        GtkTreePath *path, gpointer )
  {
    row_collapsed_or_expanded = true;
    gtk_tree_view_expand_row (view, path, true);
  }

  struct Blah
  {
    GtkTreeView * view;
    GtkTreePath * path;
    GtkTreeViewColumn * col;
  };

  gboolean maybe_activate_on_idle_idle (gpointer blah_gpointer)
  {
    Blah * blah = (Blah*) blah_gpointer;
    if (!row_collapsed_or_expanded)
      gtk_tree_view_row_activated (blah->view, blah->path, blah->col);
    gtk_tree_path_free (blah->path);
    g_free (blah);
    return false;
  }

  /**
   * There doesn't seem to be any way to see if a mouse click in a tree view
   * happened on the expander or elsewhere in a row, so when deciding whether or
   * not to activate a row on single click, let's wait and see if a row expands or
   * collapses.
   */
  void maybe_activate_on_idle (GtkTreeView *view, GtkTreePath *path, GtkTreeViewColumn *col)
  {
    row_collapsed_or_expanded = false;
    Blah * blah = (Blah*) g_new (Blah, 1);
    blah->view = view;
    blah->path = path;
    blah->col = col;
    g_idle_add (maybe_activate_on_idle_idle, blah);
  }
}

namespace
{
  static gboolean return_pressed_download_all (gpointer data)
  {
    HeaderPane * self (static_cast<HeaderPane*>(data));
    self->_gui.do_read_or_save_articles();
    return false;

  }
}

gboolean
HeaderPane :: on_keyboard_button_pressed ( GtkWidget *widget, GdkEventKey *event, gpointer data)
{
  if (event->type == GDK_KEY_PRESS)
  {
    if (event->keyval == GDK_KEY_Return)
    {
      g_idle_add(return_pressed_download_all, data);
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
HeaderPane :: on_button_pressed (GtkWidget * treeview, GdkEventButton *event, gpointer userdata)
{
  HeaderPane * self (static_cast<HeaderPane*>(userdata));
  GtkTreeView * tv (GTK_TREE_VIEW (treeview));

  // single click with the right mouse button?
  if (event->type == GDK_BUTTON_PRESS  &&  event->button == 3)
  {
    GtkTreeSelection * selection = gtk_tree_view_get_selection(tv);
    GtkTreePath *path;
    if (gtk_tree_view_get_path_at_pos (tv,
                                       (gint) event->x,
                                       (gint) event->y,
                                       &path, NULL, NULL, NULL))
    {
      if (!gtk_tree_selection_path_is_selected (selection, path))
      {
        gtk_tree_selection_unselect_all (selection);
        gtk_tree_selection_select_path (selection, path);
      }
      gtk_tree_path_free(path);
    }
    HeaderPane::do_popup_menu(treeview, event, userdata);
    return true;
  }
  else if (self->_prefs.get_flag("single-click-activates-article",true)
           && (event->type == GDK_BUTTON_RELEASE)
           && (event->button == 1)
           && (event->send_event == false)
           && (event->window == gtk_tree_view_get_bin_window (tv))
           && !(event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_MOD1_MASK))
           && (self->get_full_selection_rows_num() == 1u))
  {
    GtkTreePath * path;
    GtkTreeViewColumn * col;
    gint cell_x(0), cell_y(0);
    if (gtk_tree_view_get_path_at_pos(tv,
                                      (gint) event->x,
                                      (gint) event->y,
                                      &path, &col, &cell_x, &cell_y))
      maybe_activate_on_idle (tv, path, col);
  }
  else if ((event->type == GDK_BUTTON_RELEASE)
           && (event->button == 2)
           && (event->send_event == false)
           && (event->window == gtk_tree_view_get_bin_window (tv)))
  {
    self->_action_manager.activate_action ("clear-body-pane");
  }

  return false;
}

namespace
{
  bool has_image_type_in_subject (const Article& a)
  {
    const StringView s (a.subject.to_view());
    return s.strstr(".jpg") || s.strstr(".JPG") ||
           s.strstr(".gif") || s.strstr(".GIF") ||
           s.strstr(".jpeg") || s.strstr(".JPEG") ||
           s.strstr(".png") || s.strstr(".PNG");
  }

  gboolean on_row_activated_idle (gpointer pane_g)
  {
    HeaderPane * pane (static_cast<HeaderPane*>(pane_g));
    const Article * a (pane->get_first_selected_article());
    if (a) {
      const size_t lines = a->get_line_count();
      const bool is_smallish = lines  <= 5000;
      const bool is_mediumish = lines <= 20000;
      const bool image_subject = has_image_type_in_subject (*a);
      const bool is_pictures_newsgroup = pane->get_group().to_view().strstr("pictures")!=0;
      if (is_smallish || image_subject)
        pane->_action_manager.activate_action ("read-selected-article");
      else if (is_mediumish && is_pictures_newsgroup)
        pane->_action_manager.activate_action ("read-selected-article");
      else
        pane->_action_manager.activate_action ("save-articles");
    }
    return false;
  }
}

void
HeaderPane :: on_row_activated (GtkTreeView          * ,
                                GtkTreePath          * ,
                                GtkTreeViewColumn    * ,
                                gpointer               pane_g)
{
  g_idle_add (on_row_activated_idle, pane_g);
}

/****
*****
*****  FILTERS
*****
****/

namespace
{
  const char * mode_strings [] =
  {
    N_("Subject or Author"),
    N_("Sub or Auth (regex)"),
    N_("Subject"),
    N_("Author"),
    N_("Message-ID"),
  };

  enum
  {
    SUBJECT_OR_AUTHOR,
    SUBJECT_OR_AUTHOR_REGEX,
    SUBJECT,
    AUTHOR,
    MESSAGE_ID
  };

}

#define RANGE 4998
std::pair<int,int>
HeaderPane :: get_int_from_rules_str(std::string val)
{
  std::pair<int,int> res;
  if (val == "new")     { res.first = 0;      res.second = 0; }
  if (val == "never")   { res.first = 10;     res.second = 5; } // inversed, so never true
  if (val == "watched") { res.first = 9999;   res.second = 99999; }
  if (val == "high")    { res.first = 5000;   res.second = 5000+RANGE; }
  if (val == "medium")  { res.first = 1;      res.second = 1+RANGE; }
  if (val == "low")     { res.first = -9998;  res.second = -1; }
  if (val == "ignored") { res.first = -9999;  res.second = -99999; }
  return res;
}

void
HeaderPane :: rebuild_rules (bool enable)
{

  if (!enable)
  {
    _rules.clear();
    return;
  }

  RulesInfo &r (_rules);
  r.set_type_aggregate_and ();
  RulesInfo tmp;

  std::pair<int,int> res;

  res = get_int_from_rules_str(_prefs.get_string("rules-delete-value", "never"));
  tmp.set_type_delete_b (res.first, res.second);
  r._aggregates.push_back (tmp);

  res = get_int_from_rules_str(_prefs.get_string("rules-mark-read-value", "never"));
  tmp.set_type_mark_read_b (res.first, res.second);
  r._aggregates.push_back (tmp);

  res = get_int_from_rules_str(_prefs.get_string("rules-autocache-value", "never"));
  tmp.set_type_autocache_b (res.first, res.second);
  r._aggregates.push_back (tmp);

  res = get_int_from_rules_str(_prefs.get_string("rules-auto-dl-value", "never"));
  tmp.set_type_dl_b (res.first, res.second);
   r._aggregates.push_back (tmp);

}

void
HeaderPane :: rebuild_filter (const std::string& text, int mode)
{

  TextMatch::Description d;
  d.negate = false;
  d.case_sensitive = false;
  d.type = TextMatch::CONTAINS;
  d.text = text;

  FilterInfo &f (_filter);

  f.set_type_aggregate_and ();

  // entry field filter...
  FilterInfo entry_filter;
  if (!text.empty())
  {
    if (mode == SUBJECT)
      entry_filter.set_type_text ("Subject", d);
    else if (mode == AUTHOR)
      entry_filter.set_type_text ("From", d);
    else if (mode == MESSAGE_ID)
      entry_filter.set_type_text ("Message-ID", d);
    else if (mode == SUBJECT_OR_AUTHOR) {
      FilterInfo f1, f2;
      entry_filter.set_type_aggregate_or ();
      f1.set_type_text ("Subject", d);
      f2.set_type_text ("From", d);
      entry_filter._aggregates.push_back (f1);
      entry_filter._aggregates.push_back (f2);
    } else if (mode == SUBJECT_OR_AUTHOR_REGEX) {
      FilterInfo f1, f2;
      entry_filter.set_type_aggregate_or ();
      d.type = TextMatch::REGEX;
      f1.set_type_text ("Subject", d);
      f2.set_type_text ("From", d);
      entry_filter._aggregates.push_back (f1);
      entry_filter._aggregates.push_back (f2);
    }
    f._aggregates.push_back (entry_filter);
  }

  if (_action_manager.is_action_active("match-only-unread-articles")) {
//std::cerr << LINE_ID << " AND is unread" << std::endl;
    FilterInfo tmp;
    tmp.set_type_is_unread ();
    f._aggregates.push_back (tmp);
  }
  if (_action_manager.is_action_active("match-only-cached-articles")) {
//std::cerr << LINE_ID << " AND is cached" << std::endl;
    FilterInfo tmp;
    tmp.set_type_cached ();
    f._aggregates.push_back (tmp);
  }
  if (_action_manager.is_action_active("match-only-binary-articles")) {
//std::cerr << LINE_ID << " AND has an attachment" << std::endl;
    FilterInfo tmp;
    tmp.set_type_binary ();
    f._aggregates.push_back (tmp);
  }
  if (_action_manager.is_action_active("match-only-my-articles")) {
//std::cerr << LINE_ID << " AND was posted by me" << std::endl;
    FilterInfo tmp;
    tmp.set_type_posted_by_me ();
    f._aggregates.push_back (tmp);
  }

  // try to fold the six ranges into as few FilterInfo items as possible..
  typedef std::pair<int,int> range_t;
  std::vector<range_t> ranges;
  ranges.reserve (6);
  if (_action_manager.is_action_active("match-only-watched-articles"))
    ranges.push_back (range_t(9999, INT_MAX));
  else {
    if (_action_manager.is_action_active("match-ignored-articles")) ranges.push_back (range_t(INT_MIN, -9999));
    if (_action_manager.is_action_active("match-low-scoring-articles")) ranges.push_back (range_t(-9998, -1));
    if (_action_manager.is_action_active("match-normal-scoring-articles")) ranges.push_back (range_t(0, 0));
    if (_action_manager.is_action_active("match-medium-scoring-articles")) ranges.push_back (range_t(1, 4999));
    if (_action_manager.is_action_active("match-high-scoring-articles")) ranges.push_back (range_t(5000, 9998));
    if (_action_manager.is_action_active("match-watched-articles")) ranges.push_back (range_t(9999, INT_MAX));
  }
  for (size_t i=0; !ranges.empty() && i<ranges.size()-1; ) {
    if (ranges[i].second+1 != ranges[i+1].first)
      ++i;
    else {
      ranges[i].second = ranges[i+1].second;
      ranges.erase (ranges.begin()+i+1);
    }
  }

//for (size_t i=0; i<ranges.size(); ++i) std::cerr << LINE_ID << " range [" << ranges[i].first << "..." << ranges[i].second << "]" << std::endl;

  std::deque<FilterInfo> filters;
  for (size_t i=0; i<ranges.size(); ++i) {
    const range_t& range (ranges[i]);
    const bool low_bound (range.first == INT_MIN);
    const bool hi_bound (range.second == INT_MAX);
    if (low_bound && hi_bound) {
      // everything matches -- do nothing
    } else if (hi_bound) {
      FilterInfo tmp;
      tmp.set_type_score_ge (range.first);
//std::cerr << LINE_ID << " AND has a score >= " << range.first << std::endl;
      filters.push_back (tmp);
    } else if (low_bound) {
      FilterInfo tmp;
      tmp.set_type_score_le (range.second);
//std::cerr << LINE_ID << " AND has a score <= " << range.second << std::endl;
      filters.push_back (tmp);
    } else  { // not bound on either side; need an aggregate
      FilterInfo s, tmp;
      s.set_type_aggregate_and ();
      tmp.set_type_score_ge (range.first);
      s._aggregates.push_back (tmp);
      tmp.set_type_score_le (range.second);
      s._aggregates.push_back (tmp);
//std::cerr << LINE_ID << " AND has a in [" << range.first << "..." << range.second << ']' << std::endl;
      filters.push_back (s);
    }
  }
  if (filters.size()==1) // can fit in an `and' parent
    f._aggregates.push_back (filters[0]);
  else if (!filters.empty()) { // needs an `or' parent
    FilterInfo s;
    s.set_type_aggregate_or ();
    s._aggregates.swap (filters);
    f._aggregates.push_back (s);
  }
//std::cerr << LINE_ID << " number of filters: " << f._aggregates.size() << std::endl;
}

void
HeaderPane :: filter (const std::string& text, int mode)
{
  rebuild_filter (text, mode);

  if (_atree)
  {
    _wait.watch_cursor_on ();

    if (_filter._aggregates.empty())
      _atree->set_filter ();
    else
      _atree->set_filter (_show_type, &_filter);

    _wait.watch_cursor_off ();
  }
}

void
HeaderPane :: rules(bool enable)
{
  rebuild_rules(enable);

  if (_atree)
  {
    _wait.watch_cursor_on ();

    if (_rules._aggregates.empty())
      _atree->set_rules();
    else
      _atree->set_rules(_show_type, &_rules);

    _wait.watch_cursor_off ();
  }
}

namespace
{
  // the text typed by the user.
  std::string search_text;

  guint entry_changed_tag (0u);
  guint activate_soon_tag (0u);

  // AUTHOR, SUBJECT, SUBJECT_OR_AUTHOR, or MESSAGE_ID
  int mode;

  void set_search_entry (GtkWidget * entry, const char * s)
  {
    g_signal_handler_block (entry, entry_changed_tag);
    gtk_entry_set_text (GTK_ENTRY(entry), s);
    g_signal_handler_unblock (entry, entry_changed_tag);
  }

  gboolean search_entry_focus_in_cb (GtkWidget     * w,
                                     GdkEventFocus * ,
                                     gpointer        )
  {
#if !GTK_CHECK_VERSION(3,0,0)
    gtk_widget_modify_text (w, GTK_STATE_NORMAL, NULL); // resets
#else
    gtk_widget_override_color (w, GTK_STATE_FLAG_NORMAL, NULL);
#endif
    set_search_entry (w, search_text.c_str());
    return false;
  }

  void refresh_search_entry (GtkWidget * w)
  {
    if (search_text.empty() && !gtk_widget_has_focus(w))
    {
#if !GTK_CHECK_VERSION(3,0,0)
      GdkColor c;
      c.pixel = 0;
      c.red = c.green = c.blue = 0xAAAA;
      gtk_widget_modify_text (w, GTK_STATE_NORMAL, &c);
#else
      GdkRGBA c;
      gdk_rgba_parse (&c, "0xAAA");
      gtk_widget_override_color(w, GTK_STATE_FLAG_NORMAL, &c);
#endif
      set_search_entry (w, _(mode_strings[mode]));
    }
  }

  gboolean search_entry_focus_out_cb (GtkWidget     * w,
                                      GdkEventFocus * ,
                                      gpointer        )
  {
    refresh_search_entry (w);
    return false;
  }

  void search_activate (HeaderPane * h)
  {
    h->filter (search_text, mode);
  }

  void remove_activate_soon_tag ()
  {
    if (activate_soon_tag != 0)
    {
      g_source_remove (activate_soon_tag);
      activate_soon_tag = 0;
    }
  }

  void search_entry_activated (GtkEntry *, gpointer h_gpointer)
  {
    search_activate (static_cast<HeaderPane*>(h_gpointer));
    remove_activate_soon_tag ();
  }

  gboolean activated_timeout_cb (gpointer h_gpointer)
  {
    search_activate (static_cast<HeaderPane*>(h_gpointer));
    remove_activate_soon_tag ();
    return false; // remove the source
  }

  // ensure there's exactly one activation timeout
  // and that it's set to go off in a half second from now.
  void bump_activate_soon_tag (HeaderPane * h)
  {
    remove_activate_soon_tag ();
    activate_soon_tag = g_timeout_add (500, activated_timeout_cb, h);
  }

  // when the user changes the filter text,
  // update our state variable and bump the activate timeout.
  void search_entry_changed (GtkEditable * e, gpointer h_gpointer)
  {
    search_text = gtk_entry_get_text (GTK_ENTRY(e));
    bump_activate_soon_tag (static_cast<HeaderPane*>(h_gpointer));
    refresh_search_entry (GTK_WIDGET(e));
  }

  // when the search mode is changed via the menu,
  // update our state variable and bump the activate timeout.
  void search_menu_toggled_cb (GtkCheckMenuItem  * menu_item,
                               gpointer            entry_g)
  {
    if (gtk_check_menu_item_get_active  (menu_item))
    {
      mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT(menu_item), "MODE"));
      refresh_search_entry (GTK_WIDGET(entry_g));
      HeaderPane * h = (HeaderPane*) g_object_get_data (
                                             G_OBJECT(entry_g), "header-pane");
      bump_activate_soon_tag (h);
    }
  }

  void entry_icon_release (GtkEntry*, GtkEntryIconPosition icon_pos, GdkEventButton*, gpointer menu)
  {
    if (icon_pos == GTK_ENTRY_ICON_PRIMARY)
      gtk_menu_popup (GTK_MENU(menu), 0, 0, 0, 0, 0, gtk_get_current_event_time());
  }

  void entry_icon_release_2 (GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEventButton*, gpointer pane_gpointer)
  {
    if (icon_pos == GTK_ENTRY_ICON_SECONDARY) {
      set_search_entry (GTK_WIDGET(entry), "");
      refresh_search_entry (GTK_WIDGET(entry));
      search_text.clear ();
      search_entry_activated (NULL, pane_gpointer);
    }
  }


  void ellipsize_if_supported (GObject * o)
  {
    g_object_set (o, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  }
}

void
HeaderPane :: build_tree_columns ()
{
  GtkTreeView * tree_view (GTK_TREE_VIEW (_tree_view));
  const int xpad (_prefs.get_int ("tree-view-row-margins", 1));

  // out with the old columns, if any
  GList * old_columns = gtk_tree_view_get_columns (tree_view);
  for (GList *l=old_columns; l!=NULL; l=l->next)
    gtk_tree_view_remove_column (tree_view, GTK_TREE_VIEW_COLUMN(l->data));
  g_list_free (old_columns);

  // get the user-configurable column list
  const std::string columns (_prefs.get_string ("header-pane-columns", "state,action,subject,score,author,lines,date"));
  StringView v(columns), tok;
  while (v.pop_token (tok, ','))
  {
    const std::string& name (tok.to_string());
    const std::string width_key = std::string("header-pane-") + name +  "-column-width";
    GtkTreeViewColumn * col (0);

    if (name == "state")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_PIXBUF,
        "xpad", xpad,
        "ypad", 0,
        NULL));
      col = gtk_tree_view_column_new ();
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 24));
      gtk_tree_view_column_set_resizable (col, false);
      gtk_tree_view_column_pack_start (col, r, false);
      gtk_tree_view_column_set_cell_data_func (col, r, render_state, 0, 0);
      gtk_tree_view_column_set_sort_column_id (col, COL_STATE);
      gtk_tree_view_append_column (tree_view, col);
    }
    else if (name == "action")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_PIXBUF,
        "xpad", xpad,
        "ypad", 0,
        NULL));
      col = gtk_tree_view_column_new ();
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 24));
      gtk_tree_view_column_set_resizable (col, false);
      gtk_tree_view_column_pack_start (col, r, false);
      gtk_tree_view_column_set_cell_data_func (col, r, render_action, 0, 0);
      gtk_tree_view_append_column (tree_view, col);
    }
    else if (name == "subject")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
        "xpad", xpad,
        "ypad", 0,
        NULL));
      ellipsize_if_supported (G_OBJECT(r));
      col = gtk_tree_view_column_new_with_attributes (_("Subject"), r, NULL);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 400));
      gtk_tree_view_column_set_resizable (col, true);
      gtk_tree_view_column_set_sort_column_id (col, COL_SUBJECT);
      gtk_tree_view_column_set_cell_data_func (col, r, render_subject, this, 0);
      gtk_tree_view_append_column (tree_view, col);
      gtk_tree_view_set_expander_column (tree_view, col);
    }
    else if (name == "score")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
        "xpad", xpad,
        "ypad", 0,
        "xalign", 1.0,
        NULL));
      ellipsize_if_supported (G_OBJECT(r));
      col = gtk_tree_view_column_new_with_attributes (_("Score"), r, NULL);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 50));
      gtk_tree_view_column_set_resizable (col, true);
      gtk_tree_view_column_set_sort_column_id (col, COL_SCORE);
      gtk_tree_view_column_set_cell_data_func (col, r, render_score, this, 0);
      gtk_tree_view_append_column (tree_view, col);
    }
    else if (name == "author")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
        "xpad", xpad,
        "ypad", 0,
        NULL));
      ellipsize_if_supported (G_OBJECT(r));
      col = gtk_tree_view_column_new_with_attributes (_("Author"), r, "text", COL_SHORT_AUTHOR, NULL);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 133));
      gtk_tree_view_column_set_resizable (col, true);
      gtk_tree_view_column_set_sort_column_id (col, COL_SHORT_AUTHOR);
      gtk_tree_view_append_column (tree_view, col);
    }
    else if (name == "lines")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
        "xpad", xpad,
        "ypad", 0,
        "xalign", 1.0,
        NULL));
      ellipsize_if_supported (G_OBJECT(r));
      col = gtk_tree_view_column_new_with_attributes (_("Lines"), r, "text", COL_LINES, NULL);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 60));
      gtk_tree_view_column_set_resizable (col, true);
      gtk_tree_view_column_set_sort_column_id (col, COL_LINES);
      gtk_tree_view_append_column (tree_view, col);
    }
    else if (name == "bytes")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
        "xpad", xpad,
        "ypad", 0,
        "xalign", 1.0,
        NULL));
      ellipsize_if_supported (G_OBJECT(r));
      col = gtk_tree_view_column_new_with_attributes (_("Bytes"), r, NULL);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 80));
      gtk_tree_view_column_set_resizable (col, true);
      gtk_tree_view_column_set_sort_column_id (col, COL_BYTES);
      gtk_tree_view_column_set_cell_data_func (col, r, render_bytes, this, 0);
      gtk_tree_view_append_column (tree_view, col);
    }
    else if (name == "date")
    {
      GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT,
        "xpad", xpad,
        "ypad", 0,
        NULL));
      ellipsize_if_supported (G_OBJECT(r));
      col = gtk_tree_view_column_new_with_attributes (_("Date"), r, "text", COL_DATE_STR, NULL);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_fixed_width (col, _prefs.get_int (width_key, 120));
      gtk_tree_view_column_set_resizable (col, true);
      gtk_tree_view_column_set_sort_column_id (col, COL_DATE);
      gtk_tree_view_append_column (tree_view, col);
    }

    g_object_set_data_full (G_OBJECT(col), "column-width-key", g_strdup(width_key.c_str()), g_free);
  }
}

void
HeaderPane :: refilter ()
{
  search_activate (this);
}

void
HeaderPane :: set_show_type (const Data::ShowType show_type)
{
  if (show_type != _show_type)
  {
    _show_type = show_type;
    refilter ();
  }
}

HeaderPane :: ~HeaderPane ()
{
  if (_selection_changed_idle_tag) {
    g_source_remove (_selection_changed_idle_tag);
    _selection_changed_idle_tag  = 0;
  }

  _cache.remove_listener (this);
  _queue.remove_listener (this);
  _prefs.remove_listener (this);
  _data.remove_listener  (this);

  // save the column widths
  GList * columns = gtk_tree_view_get_columns (GTK_TREE_VIEW(_tree_view));
  for (GList *l=columns; l!=NULL; l=l->next) {
    GtkTreeViewColumn * col (GTK_TREE_VIEW_COLUMN(l->data));
    const int width = gtk_tree_view_column_get_width (col);
    const char * width_key = (const char*) g_object_get_data (G_OBJECT(col), "column-width-key");
    _prefs.set_int (width_key, width);
  }
  g_list_free (columns);

  set_group (Quark());

  for (guint i=0; i<ICON_QTY; ++i)
    g_object_unref (G_OBJECT(_icons[i].pixbuf));
}

GtkWidget*
HeaderPane :: create_filter_entry ()
{
  GtkWidget * entry = gtk_entry_new ();
  _action_manager.disable_accelerators_when_focused (entry);
  g_object_set_data (G_OBJECT(entry), "header-pane", this);
  g_signal_connect (entry, "focus-in-event", G_CALLBACK(search_entry_focus_in_cb), NULL);
  g_signal_connect (entry, "focus-out-event", G_CALLBACK(search_entry_focus_out_cb), NULL);
  g_signal_connect (entry, "activate", G_CALLBACK(search_entry_activated), this);
  entry_changed_tag = g_signal_connect (entry, "changed", G_CALLBACK(search_entry_changed), this);

      gtk_entry_set_icon_from_stock( GTK_ENTRY( entry ),
                                   GTK_ENTRY_ICON_PRIMARY,
                                   GTK_STOCK_FIND);
   gtk_entry_set_icon_from_stock( GTK_ENTRY( entry ),
                                  GTK_ENTRY_ICON_SECONDARY,
                                  GTK_STOCK_CLEAR );

  bool regex = _prefs.get_flag ("use-regex", false);
  GtkWidget * menu = gtk_menu_new ();
  if (regex == true )
    mode = 1;
  else
    mode = 0;
  GSList * l = 0;
  for (int i=0, qty=G_N_ELEMENTS(mode_strings); i<qty; ++i) {
    GtkWidget * w = gtk_radio_menu_item_new_with_label (l, _(mode_strings[i]));
    l = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM(w));
    g_object_set_data (G_OBJECT(w), "MODE", GINT_TO_POINTER(i));
    g_signal_connect (w, "toggled", G_CALLBACK(search_menu_toggled_cb),entry);
    if (mode == i)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(w), TRUE);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), w);
    gtk_widget_show (w);
  }
  g_signal_connect (entry, "icon-release", G_CALLBACK(entry_icon_release), menu);
  g_signal_connect (entry, "icon-release", G_CALLBACK(entry_icon_release_2), this);

  refresh_search_entry (entry);

  return entry;
}

void
HeaderPane :: on_selection_changed (GtkTreeSelection*, gpointer self_gpointer)
{
  HeaderPane * self (static_cast<HeaderPane*>(self_gpointer));

  if (!self->_selection_changed_idle_tag)
       self->_selection_changed_idle_tag = g_idle_add (on_selection_changed_idle, self);
}

gboolean
HeaderPane :: on_selection_changed_idle (gpointer self_gpointer)
{
  HeaderPane * self (static_cast<HeaderPane*>(self_gpointer));
  const bool have_article = self->get_first_selected_article() != 0;
  static const char* actions_that_need_an_article[] = {
    "download-selected-article",
    "save-articles",
    "save-articles-from-nzb",
    "save-articles-to-nzb",
    "read-selected-article",
    "show-selected-article-info",
    "mark-article-read",
    "mark-article-unread",
    "mark-thread-read",
    "mark-thread-unread",
    "watch-thread",
    "ignore-thread",
    "plonk",
    "view-article-score",
    "delete-article",
    "followup-to",
    "reply-to",
    "supersede-article",
    "cancel-article"
  };

  for (int i=0, n=G_N_ELEMENTS(actions_that_need_an_article); i<n; ++i)
    self->_action_manager.sensitize_action (actions_that_need_an_article[i], have_article);

  self->_selection_changed_idle_tag = 0;
  return false;
}

Data::ShowType _show_type;

HeaderPane :: HeaderPane (ActionManager       & action_manager,
                          Data                & data,
                          Queue               & queue,
                          ArticleCache        & cache,
                          Prefs               & prefs,
                          GroupPrefs          & group_prefs,
                          WaitUI              & wait,
                          GUI                 & gui):
  _action_manager (action_manager),
  _data (data),
  _queue (queue),
  _prefs (prefs),
  _group_prefs (group_prefs),
  _wait (wait),
  _atree (0),
  _root (0),
  _tree_view (0),
  _tree_store (0),
  _selection_changed_idle_tag (0),
  _cache (cache),
  _gui (gui)
{
  // init the icons
  for (guint i=0; i<ICON_QTY; ++i)
    _icons[i].pixbuf = gdk_pixbuf_new_from_inline (-1, _icons[i].pixbuf_txt, FALSE, 0);

  // initialize the show type...
  const std::string show_type_str (prefs.get_string ("header-pane-show-matching", "articles"));
  if (show_type_str == "threads")
    _show_type = Data::SHOW_THREADS;
  else if (show_type_str == "subthreads")
    _show_type = Data::SHOW_SUBTHREADS;
  else
    _show_type = Data::SHOW_ARTICLES;

  // build the view...
  GtkWidget * w = _tree_view = gtk_tree_view_new ();
  gtk_tree_view_set_enable_search (GTK_TREE_VIEW(w), false);
  gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW(w), true);
  gtk_tree_view_set_rubber_banding (GTK_TREE_VIEW(w), true);

  GtkTreeSelection * sel = gtk_tree_view_get_selection (GTK_TREE_VIEW(w));
  gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
  g_signal_connect (sel, "changed", G_CALLBACK(on_selection_changed), this);
  on_selection_changed (sel, this);


  g_signal_connect (w, "button-release-event", G_CALLBACK(on_button_pressed), this);
  g_signal_connect (w, "button-press-event", G_CALLBACK(on_button_pressed), this);

  /* intercept ENTER for selection of multiple articles */
  g_signal_connect (w, "key-press-event", G_CALLBACK(on_keyboard_button_pressed), this);

  g_signal_connect (w, "row-collapsed", G_CALLBACK(row_collapsed_cb), NULL);
  g_signal_connect (w, "row-expanded", G_CALLBACK(row_expanded_cb), NULL);
  g_signal_connect (w, "popup-menu", G_CALLBACK(on_popup_menu), this);
  g_signal_connect (w, "row-activated", G_CALLBACK(on_row_activated), this);
  GtkWidget * scroll = gtk_scrolled_window_new (0, 0);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER(scroll), w);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);

  build_tree_columns ();

  // join rows
  _root = scroll;

  search_activate (this); // calls rebuild_filter
  rules(_prefs._rules_enabled);

  _data.add_listener (this);
  _prefs.add_listener (this);
  _queue.add_listener (this);
  _cache.add_listener (this);

  refresh_font ();
}

void
HeaderPane :: set_focus ()
{
  gtk_widget_grab_focus (_tree_view);
}

void
HeaderPane :: select_all ()
{
  GtkTreeView * tv (GTK_TREE_VIEW (_tree_view));
  GtkTreeSelection * sel (gtk_tree_view_get_selection (tv));
  gtk_tree_selection_select_all (sel);
}

void
HeaderPane :: unselect_all ()
{
  GtkTreeView * tv (GTK_TREE_VIEW (_tree_view));
  GtkTreeSelection * sel (gtk_tree_view_get_selection (tv));
  gtk_tree_selection_unselect_all (sel);
}

namespace
{
  void select_subpaths (GtkTreeView * view, GtkTreeModel * model, GtkTreeSelection * sel, GtkTreePath * begin_path)
  {
    gtk_tree_view_expand_row (view, begin_path, true);
    GtkTreeIter begin;
    gtk_tree_model_get_iter (model, &begin, begin_path);
    GtkTreeIter end = begin;
    int n;
    while ((n = gtk_tree_model_iter_n_children (model, &end))) {
      GtkTreeIter tmp = end;
      gtk_tree_model_iter_nth_child (model, &end, &tmp, n-1);
    }
    GtkTreePath * end_path = gtk_tree_model_get_path (model,  &end);
    gtk_tree_selection_select_range (sel, begin_path, end_path);
    gtk_tree_path_free (end_path);
  }

  void select_threads_helper (GtkTreeView * view, bool subthreads_only)
  {
    GtkTreeModel * model;
    GtkTreeSelection * sel (gtk_tree_view_get_selection (view));
    GList * list (gtk_tree_selection_get_selected_rows (sel, &model));

    for (GList *l(list); l; l=l->next) {
      GtkTreePath * path (static_cast<GtkTreePath*>(l->data));
      if (!subthreads_only)
        while (gtk_tree_path_get_depth (path) > 1) // up to root
        gtk_tree_path_up (path);
      select_subpaths (view, model, sel, path);
    }

    g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
    g_list_free (list);
  }
}

void
HeaderPane :: select_threads ()
{
  select_threads_helper (GTK_TREE_VIEW(_tree_view), false);
}

void
HeaderPane :: select_subthreads ()
{
  select_threads_helper (GTK_TREE_VIEW(_tree_view), true);
}

void
HeaderPane :: expand_selected ()
{
  // get a list of paths
  GtkTreeModel * model (0);
  GtkTreeView * view (GTK_TREE_VIEW (_tree_view));
  GtkTreeSelection * selection (gtk_tree_view_get_selection (view));
  GList * list (gtk_tree_selection_get_selected_rows (selection, &model));
  if (list)
  {
    GtkTreePath * path (static_cast<GtkTreePath*>(list->data));
    gtk_tree_view_expand_row (view, path, true);
    gtk_tree_view_expand_to_path (view, path);
  }
  g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
  g_list_free (list);
}

/***
****
****  NAVIGATION
****
***/

namespace
{
  /**
  ***  Navigation Functors
  **/

  struct TreeIteratorNext: public TreeIterFunctor {
    virtual ~TreeIteratorNext () {}
    virtual bool operator ()(GtkTreeModel * model, GtkTreeIter * iter) const {
      return model && PAN_TREE_STORE(model)->get_next (iter);
    }
    virtual bool front (GtkTreeModel* model, GtkTreeIter* setme) const {
      return model && PAN_TREE_STORE(model)->front (setme);
    }
  };

  struct TreeIteratorPrev: public TreeIterFunctor {
    virtual ~TreeIteratorPrev () {}
    bool operator()(GtkTreeModel * model, GtkTreeIter * iter) const {
      return model && PAN_TREE_STORE(model)->get_prev (iter);
    }
    virtual bool front (GtkTreeModel* model, GtkTreeIter* setme) const {
      return model && PAN_TREE_STORE(model)->back (setme);
    }
  };

  /**
  ***  Article Test Functors
  **/

  struct ArticleExists: public ArticleTester {
    virtual ~ArticleExists() {}
    ArticleExists () {}
    virtual bool operator()(const Article&) const { return true; }
  };

  struct ArticleIsFlagged: public ArticleTester {
    virtual ~ArticleIsFlagged () {}
    const Article* article;
    ArticleIsFlagged (const Article* a) : article(a) {}
    virtual bool operator()(const Article& a) const
      { return a.get_flag() && a.message_id != article->message_id; }
  };

  struct ArticleIsParentOf: public ArticleTester {
    virtual ~ArticleIsParentOf () {}
    ArticleIsParentOf (const Data::ArticleTree& tree, const Article* a) {
      const Article * parent = a ? tree.get_parent(a->message_id) : 0;
      _mid = parent ? parent->message_id : "";
    }
    Quark _mid;
    virtual bool operator()(const Article& a) const { return _mid==a.message_id; }
  };

  struct ArticleIsUnread: public ArticleTester {
    virtual ~ArticleIsUnread () {}
    ArticleIsUnread (const Data& data): _data(data) {}
    const Data& _data;
    virtual bool operator()(const Article& a) const { return !_data.is_read(&a); }
  };

  struct ArticleIsNotInThread: public ArticleTester {
    virtual ~ArticleIsNotInThread () {}
    ArticleIsNotInThread (const Data::ArticleTree& tree, const Article* a):
      _tree(tree),
      _root(a ? get_root_mid(a) : "") {}
    const Data::ArticleTree& _tree;
    const Quark _root;
    Quark get_root_mid (const Article * a) const {
      for (;;) {
        const Quark mid (a->message_id);
        const Article * parent = _tree.get_parent (mid);
        if (!parent)
          return mid;
        a = parent;
      }
    }
    virtual bool operator()(const Article& a) const {
      return _root != get_root_mid(&a);
    }
  };

  struct ArticleIsUnreadAndNotInThread: public ArticleTester {
    virtual ~ArticleIsUnreadAndNotInThread () {}
    ArticleIsUnreadAndNotInThread (const Data& data, const Data::ArticleTree& tree, const Article* a): _aiu (data), _ainit(tree, a) {}
    const ArticleIsUnread _aiu;
    const ArticleIsNotInThread _ainit;
    virtual bool operator()(const Article& a) const {
      return _aiu(a) && _ainit(a);
    }
  };

  /**
  ***  Action Functors
  **/

  struct SelectFunctor: public pan::RowActionFunctor {
    virtual ~SelectFunctor () {}
    SelectFunctor (GtkTreeView * view): _view(view) {}
    GtkTreeView * _view;
    virtual void operator() (GtkTreeModel* model, GtkTreeIter* iter, const Article&) {
      GtkTreeSelection * sel (gtk_tree_view_get_selection (_view));
      gtk_tree_selection_unselect_all (sel);
      GtkTreePath * path = gtk_tree_model_get_path (model, iter);
      gtk_tree_view_expand_row (_view, path, true);
      gtk_tree_view_expand_to_path (_view, path);
      gtk_tree_view_set_cursor (_view, path, NULL, FALSE);
      gtk_tree_view_scroll_to_cell (_view, path, NULL, true, 0.5f, 0.0f);
      gtk_tree_path_free (path);
    }
  };

  struct ReadFunctor: public SelectFunctor {
    virtual ~ReadFunctor() {}
    ReadFunctor (GtkTreeView * view, ActionManager& am): SelectFunctor(view), _am(am) {}
    ActionManager& _am;
    virtual void operator() (GtkTreeModel* model, GtkTreeIter* iter, const Article& a) {
      SelectFunctor::operator() (model, iter, a);
      maybe_activate_on_idle (_view, gtk_tree_model_get_path(model,iter), 0);
    }
  };
}

/**
***
**/

void
HeaderPane :: find_next_iterator_from (GtkTreeModel            * model,
                                       GtkTreeIter             * start_pos,
                                       const TreeIterFunctor   & iterate_func,
                                       const ArticleTester     & test_func,
                                       RowActionFunctor        & success_func,
                                       bool                      test_the_start_pos)
{
  g_assert (start_pos!=0);
  GtkTreeIter march = *start_pos;
  bool success (false);
  const Article *article (0);
  for (;;)
  {
    if (test_the_start_pos)
      test_the_start_pos = false;
    else {
      if (!iterate_func (model, &march))
	break;
      if ((start_pos->stamp == march.stamp)  // PanTreeStore iter equality:
	   && (start_pos->user_data == march.user_data)) // have we looped?
	break;
    }
    article = get_article (model, &march);
    if ((success = test_func (*article)))
      break;
  }
  if (success)
    success_func (model, &march, *article);
}

namespace
{
  bool get_first_selection (GtkTreeSelection * sel, GtkTreeIter * setme)
  {
    GtkTreeModel * model (0);
    GList * list (gtk_tree_selection_get_selected_rows (sel, &model));
    const bool found (list && gtk_tree_model_get_iter (model, setme, (GtkTreePath*)(list->data)));
    g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
    g_list_free (list);
    return found;
  }
}

void
HeaderPane :: next_iterator (GtkTreeView            * view,
                             GtkTreeModel           * model,
                             const TreeIterFunctor  & iterate_func,
                             const ArticleTester    & test_func,
                             RowActionFunctor       & success_func)
{
  GtkTreeSelection * sel = gtk_tree_view_get_selection (view);
  GtkTreeIter iter;
  if (get_first_selection (sel, &iter))
    find_next_iterator_from (model, &iter, iterate_func, test_func, success_func, false);
  else if (iterate_func.front (model, &iter))
    find_next_iterator_from (model, &iter, iterate_func, test_func, success_func, true);
}

void
HeaderPane :: action_next_if (const ArticleTester& test, RowActionFunctor& action)
{
  GtkTreeView * v (GTK_TREE_VIEW(_tree_view));
  GtkTreeModel * m (GTK_TREE_MODEL(_tree_store));
  next_iterator (v, m, TreeIteratorNext(), test, action);
}

void
HeaderPane :: read_next_if (const ArticleTester& test)
{

  GtkTreeView * v (GTK_TREE_VIEW(_tree_view));
  ReadFunctor read (v, _action_manager);
  action_next_if (test, read);
}

void
HeaderPane :: read_prev_if (const ArticleTester & test)
{

  GtkTreeView * v (GTK_TREE_VIEW(_tree_view));
  GtkTreeModel * m (GTK_TREE_MODEL(_tree_store));
  ReadFunctor read (v, _action_manager);
  next_iterator (v, m, TreeIteratorPrev(), test, read);
}

void
HeaderPane :: select_next_if (const ArticleTester& test)
{

  GtkTreeView * v (GTK_TREE_VIEW(_tree_view));
  SelectFunctor sel (v);
  action_next_if (test, sel);
}

void
HeaderPane :: select_prev_if (const ArticleTester& test)
{

  GtkTreeView * v (GTK_TREE_VIEW(_tree_view));
  GtkTreeModel * m (GTK_TREE_MODEL(_tree_store));
  SelectFunctor sel (v);
  next_iterator (v, m, TreeIteratorPrev(), test, sel);
}

void
HeaderPane :: read_next_unread_article ()
{
  read_next_if (ArticleIsUnread(_data));
}

void
HeaderPane :: read_previous_article ()
{
  read_prev_if (ArticleExists());
}

void
HeaderPane :: read_next_article ()
{
  read_next_if (ArticleExists());
}

void
HeaderPane :: read_next_thread ()
{
  if (_atree)
    read_next_if (ArticleIsNotInThread(*_atree, get_first_selected_article()));
}

void
HeaderPane :: read_next_unread_thread ()
{
  if (_atree)
    read_next_if (ArticleIsUnreadAndNotInThread(_data, *_atree, get_first_selected_article()));
}

void
HeaderPane :: read_previous_thread ()
{
  if (_atree)
    read_prev_if (ArticleIsNotInThread(*_atree, get_first_selected_article()));
}

void
HeaderPane :: read_parent_article ()
{
  if (_atree)
    read_prev_if (ArticleIsParentOf(*_atree, get_first_selected_article()));
}

/***
****
***/

void
HeaderPane :: move_to_next_bookmark(int step)
{
  const bool forward(step == 1);
  Article * a (get_first_selected_article());
  if (!a) return;

  if (forward)
    select_next_if (ArticleIsFlagged(get_first_selected_article()));
  else
    select_prev_if (ArticleIsFlagged(get_first_selected_article()));
}

/***
****
***/

void
HeaderPane :: refresh_font ()
{
  if (!_prefs.get_flag ("header-pane-font-enabled", false))
    gtk_widget_override_font (_tree_view, 0);
  else {
    const std::string str (_prefs.get_string ("header-pane-font", "Sans 10"));
    PangoFontDescription * pfd (pango_font_description_from_string (str.c_str()));
    gtk_widget_override_font (_tree_view, pfd);
    pango_font_description_free (pfd);
  }
}

void
HeaderPane :: on_prefs_flag_changed (const StringView& key, bool)
{
  if (key == "header-pane-font-enabled")
    refresh_font ();
  if (key == "thread-headers")
    rebuild ();
}

void
HeaderPane :: on_prefs_string_changed (const StringView& key, const StringView&)
{
  if (key == "header-pane-font")
    refresh_font ();
  else if (key == "header-pane-columns")
    build_tree_columns ();
}

/***
****
***/

void
HeaderPane :: rebuild_article_action (const Quark& message_id)
{
  Row * row (get_row (message_id));
  if (row) {
    row->action = get_article_action (row->article, _cache, _queue, message_id);
    _tree_store->row_changed (row);
  }
}

void
HeaderPane :: on_article_flag_changed (articles_t& a, const Quark& group)
{
  g_return_if_fail(!a.empty());
  foreach_const (articles_t, a, it)
    rebuild_article_action ((*it)->message_id);
}

void
HeaderPane :: on_queue_tasks_added (Queue& queue, int index, int count)
{
  for (size_t i(index), end(index+count); i!=end; ++i) {
    const TaskArticle * task (dynamic_cast<const TaskArticle*>(queue[i]));
    if (task)
      rebuild_article_action (task->get_article().message_id);
  }
}

void
HeaderPane :: on_queue_task_removed (Queue&, Task& task, int)
{
  const TaskArticle * ta (dynamic_cast<const TaskArticle*>(&task));
  if (ta)
    rebuild_article_action (ta->get_article().message_id);
}

void
HeaderPane :: on_cache_added (const Quark& message_id)
{
  quarks_t q;
  q.insert(message_id);
  _data.rescore_articles ( _group, q );
  rebuild_article_action (message_id);

}
void
HeaderPane :: on_cache_removed (const quarks_t& message_ids)
{
  foreach_const (quarks_t, message_ids, it)
    rebuild_article_action (*it);
}

/***
****
***/

struct HeaderPane::SimilarWalk: public PanTreeStore::WalkFunctor
{
  private:
    GtkTreeSelection * selection;
    const Article source;

  public:
    virtual ~SimilarWalk() {}
    SimilarWalk (GtkTreeSelection * s, const Article& a): selection(s), source(a) {}

  public:
    virtual bool operator()(PanTreeStore * store, PanTreeStore::Row *,
			    GtkTreeIter * iter, GtkTreePath *) {
      const Article * article (get_article (GTK_TREE_MODEL(store), iter));
      if (similar (*article))
	gtk_tree_selection_select_iter (selection, iter);
      return true; // keep marching
    }

  private:

    bool similar (const Article& a) const
    {
      // same author, posted within a day and a half of the source, with a similar subject
      static const size_t SECONDS_IN_DAY (60 * 60 * 24);
      return (a.author == source.author)
	&& (fabs (difftime (a.time_posted, source.time_posted)) < (SECONDS_IN_DAY * 1.5))
	&& (subjects_are_similar (a, source));
    }

    static bool subjects_are_similar (const Article& a, const Article& b)
    {
      // make our own copies of the strings so that we can mutilate them
      std::string sa (a.subject.c_str());
      std::string sb (b.subject.c_str());

      // strip out frequent substrings that tend to skew string_likeness too high
      static const char * const frequent_substrings [] = { "mp3", "gif", "jpg", "jpeg", "yEnc" };
      for (size_t i=0; i!=G_N_ELEMENTS(frequent_substrings); ++i) {
        std::string::size_type pos;
        const char * needle (frequent_substrings[i]);
        while (((pos = sa.find (needle))) != std::string::npos) sa.erase (pos, strlen(needle));
        while (((pos = sb.find (needle))) != std::string::npos) sb.erase (pos, strlen(needle));
      }

      // strip out non-alpha characters
      foreach (std::string, sa, it) { if (!isalpha(*it)) *it = ' '; }
      foreach (std::string, sb, it) { if (!isalpha(*it)) *it = ' '; }

      // decide how picky we want to be.
      // The shorter the string, the more alike they have to be.
      // longer strings typically include long unique filenames.
      const int min_len (std::min (sa.size(), sb.size()));
      const bool is_short_string (min_len <= 20);
      const bool is_long_string (min_len >= 30);
      double min_closeness;
      if (is_short_string)
        min_closeness = 0.6;
      else if (is_long_string)
        min_closeness = 0.5;
      else
        min_closeness = 0.55;

      return string_likeness (sa, sb) >= min_closeness;
    }

    static double string_likeness (const std::string& a_in, const std::string& b_in)
    {
      double retval;
      StringView a(a_in), b(b_in);

      if (!a.strchr(' ')) // only one word, so count common characters
      {
        int common_chars = 0;

        foreach_const (StringView, a, it) {
          const char * pos = b.strchr (*it);
          if (pos) {
            ++common_chars;
            b.eat_chars (pos - b.str);
          }
        }

        retval = (double)common_chars / a.len;
      }
        else // more than one word, so count common words
      {
        StringView tok;
        int str1_words(0), common_words(0);
        while (a.pop_token (tok)) {
          ++str1_words;
          const char *pch = b.strstr (tok);
          if (pch) ++common_words;
        }
        retval = (double)common_words / str1_words;
      }
      return retval;
    }
};

void
HeaderPane :: select_similar ()
{
  GtkTreeSelection * sel (gtk_tree_view_get_selection (GTK_TREE_VIEW (_tree_view)));
  const Article * article (get_first_selected_article ());
  if (article) {
    SimilarWalk similar (sel, *article);
    _tree_store->walk (similar);
  }
}
