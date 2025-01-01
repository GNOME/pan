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

#include "header-pane.h"
#include "pan/gui/load-icon.h"
#include "render-bytes.h"
#include "tango-colors.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <config.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/general/debug.h>
#include <pan/general/e-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/quark.h>
#include <pan/general/utf8-utils.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/rules-info.h>

using namespace pan;

/****
*****
****/

namespace {
enum
{
  COL_DATE_STR,
  COL_STATE,
  COL_ACTION,
  COL_SCORE,
  COL_LINES,
  COL_BYTES,
  COL_DATE,
  COL_ARTICLE_POINTER,
  COL_SUBJECT,
  COL_SHORT_AUTHOR,
  N_COLUMNS
};
}

namespace {
// default color theme's Colors
PanColors const &colors(PanColors::get());
} // namespace

Article const *HeaderPane ::get_article(GtkTreeModel *model, GtkTreeIter *iter)
{
  Article const *a =
    dynamic_cast<Row *>(PAN_TREE_STORE(model)->get_row(iter))->article;
  g_assert(a != nullptr);
  return a;
}

/****
*****
****/

namespace {
typedef std::set<Article const *> articles_t;

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
  ICON_GET_FLAGGED,
  ICON_QTY
};

struct Icon
{
    char const *icon_file;
    GdkPixbuf *pixbuf;
} _icons[ICON_QTY] = {{"icon_article_read.png", nullptr},
                      {"icon_article_unread.png", nullptr},

                      {"icon_binary_complete.png", nullptr},
                      {"icon_binary_complete_read.png", nullptr},

                      {"icon_binary_incomplete.png", nullptr},
                      {"icon_binary_incomplete_read.png", nullptr},

                      {"icon_disk.png", nullptr},
                      {"icon_bluecheck.png", nullptr},
                      {"icon_x.png", nullptr},
                      {"icon_empty.png", nullptr},
                      {"icon_red_flag.png", nullptr},
                      {"icon_get_flagged.png", nullptr}};

int get_article_state(Data const &data, Article const *a)
{
  int retval;
  bool const read(data.is_read(a));
  int const part_state(a->get_part_state());

  if (part_state == Article::COMPLETE && read)
  {
    retval = ICON_COMPLETE_READ;
  }
  else if (part_state == Article::COMPLETE)
  {
    retval = ICON_COMPLETE;
  }
  else if (part_state == Article::INCOMPLETE && read)
  {
    retval = ICON_INCOMPLETE_READ;
  }
  else if (part_state == Article::INCOMPLETE)
  {
    retval = ICON_INCOMPLETE;
  }
  else if (read)
  {
    retval = ICON_READ;
  }
  else
  {
    retval = ICON_UNREAD;
  }

  return retval;
}

int get_article_action(Article const *article,
                       ArticleCache const &cache,
                       Queue const &queue,
                       Quark const &message_id)
{
  int offset(ICON_EMPTY);

  if (queue.contains(message_id))
  {
    offset = ICON_QUEUED;
  }
  else if (cache.contains(message_id))
  {
    offset = ICON_CACHED;
  }

  if (article)
  {
    if (article->flag)
    {
      offset = ICON_FLAGGED;
    }
  }

  return offset;
}
}; // namespace

void HeaderPane ::render_action(GtkTreeViewColumn *,
                                GtkCellRenderer *renderer,
                                GtkTreeModel *model,
                                GtkTreeIter *iter,
                                gpointer)
{
  int index(0);
  gtk_tree_model_get(model, iter, COL_ACTION, &index, -1);
  g_object_set(renderer, "pixbuf", _icons[index].pixbuf, nullptr);
}

void HeaderPane ::render_state(GtkTreeViewColumn *,
                               GtkCellRenderer *renderer,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer)
{
  int index(0);
  gtk_tree_model_get(model, iter, COL_STATE, &index, -1);
  g_object_set(renderer, "pixbuf", _icons[index].pixbuf, nullptr);
}

int HeaderPane ::find_highest_followup_score(GtkTreeModel *model,
                                             GtkTreeIter *parent) const
{
  int score(-9999);
  GtkTreeIter child;
  if (gtk_tree_model_iter_children(model, &child, parent))
  {
    do
    {
      Article const *a(get_article(model, &child));
      score = std::max(score, a->score);
      score = std::max(score, find_highest_followup_score(model, &child));
    } while (gtk_tree_model_iter_next(model, &child));
  }
  return score;
}

void HeaderPane ::render_score(GtkTreeViewColumn *,
                               GtkCellRenderer *renderer,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer user_data)
{
  int score(0);
  gtk_tree_model_get(model, iter, COL_SCORE, &score, -1);

  char buf[16];
  if (score)
  {
    g_snprintf(buf, sizeof(buf), "%d", score);
  }
  else
  {
    *buf = '\0';
  }

  HeaderPane *self(static_cast<HeaderPane *>(user_data));
  GtkTreeView *view(GTK_TREE_VIEW(self->_tree_view));
  GtkTreePath *path(gtk_tree_model_get_path(model, iter));
  bool const expanded(gtk_tree_view_row_expanded(view, path));
  gtk_tree_path_free(path);
  if (! expanded)
  {
    score = std::max(score, self->find_highest_followup_score(model, iter));
  }

  Prefs const &prefs(self->_prefs);
  std::string bg, fg;
  if (score >= 9999)
  {
    fg = prefs.get_color_str("score-color-watched-fg", colors.def_fg);
    bg = prefs.get_color_str("score-color-watched-bg", TANGO_CHAMELEON_LIGHT);
  }
  else if (score >= 5000)
  {
    fg = prefs.get_color_str("score-color-high-fg", colors.def_fg);
    bg = prefs.get_color_str("score-color-high-bg", TANGO_BUTTER_LIGHT);
  }
  else if (score >= 1)
  {
    fg = prefs.get_color_str("score-color-medium-fg", colors.def_fg);
    bg = prefs.get_color_str("score-color-medium-bg", TANGO_SKY_BLUE_LIGHT);
  }
  else if (score <= -9999)
  {
    fg = prefs.get_color_str("score-color-ignored-fg", TANGO_ALUMINUM_4);
    bg = prefs.get_color_str("score-color-ignored-bg", colors.def_bg);
  }
  else if (score <= -1)
  {
    fg = prefs.get_color_str("score-color-low-fg", TANGO_ALUMINUM_2);
    bg = prefs.get_color_str("score-color-low-bg", colors.def_bg);
  }
  else if (score == 0)
  {
    fg = self->_fg;
    bg = self->_bg;
  }

  g_object_set(renderer,
               "text",
               buf,
               "background",
               (bg.empty() ? nullptr : bg.c_str()),
               "foreground",
               (fg.empty() ? nullptr : fg.c_str()),
               nullptr);
}

void HeaderPane ::render_author(GtkTreeViewColumn *,
                                GtkCellRenderer *renderer,
                                GtkTreeModel *model,
                                GtkTreeIter *iter,
                                gpointer user_data)
{
  HeaderPane const *self(static_cast<HeaderPane *>(user_data));
  Article const *a(self->get_article(model, iter));

  g_object_set(renderer,
               "text",
               a->author.c_str(),
               "background",
               self->_bg.c_str(),
               "foreground",
               self->_fg.c_str(),
               nullptr);
}

void HeaderPane ::render_lines(GtkTreeViewColumn *,
                               GtkCellRenderer *renderer,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer user_data)
{

  HeaderPane const *self(static_cast<HeaderPane *>(user_data));

  unsigned long lines(0ul);
  std::stringstream str;

  gtk_tree_model_get(model, iter, COL_LINES, &lines, -1);
  str << lines;

  g_object_set(renderer,
               "text",
               str.str().c_str(),
               "background",
               self->_bg.c_str(),
               "foreground",
               self->_fg.c_str(),
               nullptr);
}

void HeaderPane ::render_bytes(GtkTreeViewColumn *,
                               GtkCellRenderer *renderer,
                               GtkTreeModel *model,
                               GtkTreeIter *iter,
                               gpointer userdata)
{
  HeaderPane const *self(static_cast<HeaderPane *>(userdata));

  unsigned long bytes(0);
  gtk_tree_model_get(model, iter, COL_BYTES, &bytes, -1);
  g_object_set(renderer,
               "text",
               pan::render_bytes(bytes),
               "background",
               self->_bg.c_str(),
               "foreground",
               self->_fg.c_str(),
               nullptr);
}

void HeaderPane ::render_date(GtkTreeViewColumn *,
                              GtkCellRenderer *renderer,
                              GtkTreeModel *model,
                              GtkTreeIter *iter,
                              gpointer userdata)
{
  HeaderPane const *self(static_cast<HeaderPane *>(userdata));

  gchar *date(nullptr);
  gtk_tree_model_get(model, iter, COL_DATE_STR, &date, -1);
  g_object_set(renderer,
               "text",
               date,
               "background",
               self->_bg.c_str(),
               "foreground",
               self->_fg.c_str(),
               nullptr);
  g_free(date);
}

struct HeaderPane::CountUnread : public PanTreeStore::WalkFunctor
{
    Row const *top_row;
    unsigned long unread_children;

    CountUnread(Row const *top) :
      top_row(top),
      unread_children(0)
    {
    }

    virtual ~CountUnread()
    {
    }

    virtual bool operator()(PanTreeStore *,
                            PanTreeStore::Row *r,
                            GtkTreeIter *,
                            GtkTreePath *)
    {
      Row const *row(dynamic_cast<Row *>(r));
      if (row != top_row && ! row->is_read)
      {
        ++unread_children;
      }
      return true;
    }
};

void HeaderPane ::render_subject(GtkTreeViewColumn *,
                                 GtkCellRenderer *renderer,
                                 GtkTreeModel *model,
                                 GtkTreeIter *iter,
                                 gpointer user_data)
{
  HeaderPane const *self(static_cast<HeaderPane *>(user_data));
  Prefs const &p(self->_prefs);
  Row const *row(dynamic_cast<Row *>(self->_tree_store->get_row(iter)));

  CountUnread counter(row);
  self->_tree_store->prefix_walk(counter, iter);

  bool const bold(! row->is_read);

  Article const *a(self->get_article(model, iter));

  std::string res = a->subject.c_str();

  char buf[512];

  bool unread(false);

  if (counter.unread_children)
  {
    // find out if the row is expanded or not
    GtkTreeView *view(GTK_TREE_VIEW(self->_tree_view));
    GtkTreePath *path(gtk_tree_model_get_path(model, iter));
    bool const expanded(gtk_tree_view_row_expanded(view, path));
    gtk_tree_path_free(path);

    if (! expanded)
    {
      unread = row->is_read;
      snprintf(
        buf, sizeof(buf), "%s (%lu)", res.c_str(), counter.unread_children);
      res = buf;
    }
  }

  std::string def_bg, def_fg;
  def_fg = p.get_color_str_wo_fallback("color-read-fg");
  def_bg = p.get_color_str_wo_fallback("color-read-bg");

  g_object_set(renderer,
               "text",
               res.c_str(),
               "weight",
               (bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL),
               "foreground",
               unread ? (def_fg.empty() ? self->_fg.c_str() : def_fg.c_str()) :
                        self->_fg.c_str(),
               "background",
               unread ? (def_bg.empty() ? self->_bg.c_str() : def_bg.c_str()) :
                        self->_bg.c_str(),
               nullptr);
}

HeaderPane::Row *HeaderPane ::get_row(Quark const &message_id)
{
  mid_to_row_t::iterator it(_mid_to_row.find(message_id));
  return it == _mid_to_row.end() ? nullptr : *it;
}

HeaderPane::Row *HeaderPane ::create_row(EvolutionDateMaker const &e,
                                         Article const *a)
{
  int const action(get_article_action(a, _cache, _queue, a->message_id));
  int const state(get_article_state(_data, a));
  char *date_str(e.get_date_string(a->time_posted));
  Row *row = new Row(_data, a, date_str, action, state);

  std::pair<mid_to_row_t::iterator, bool> result(_mid_to_row.insert(row));
  g_assert(result.second);

  return row;
}

void HeaderPane ::add_children_to_model(PanTreeStore *store,
                                        PanTreeStore::Row *parent_row,
                                        Quark const &parent_mid,
                                        Data::ArticleTree const *atree,
                                        EvolutionDateMaker const &date_maker,
                                        bool const do_thread)
{
  // see if this parent has any children...
  article_v children;
  atree->get_children(parent_mid, children);
  if (children.empty())
  {
    return;
  }

  // add all these children...
  PanTreeStore::rows_t rows;
  rows.reserve(children.size());
  foreach_const (article_v, children, it)
  {
    rows.push_back(create_row(date_maker, *it));
  }
  store->append(do_thread ? parent_row : nullptr, rows);

  // recurse
  for (size_t i = 0, n = children.size(); i < n; ++i)
  {
    add_children_to_model(
      store, rows[i], children[i]->message_id, atree, date_maker, do_thread);
  }
}

int HeaderPane ::column_compare_func(GtkTreeModel *model,
                                     GtkTreeIter *iter_a,
                                     GtkTreeIter *iter_b,
                                     gpointer)
{
  int ret(0);
  PanTreeStore const *store(reinterpret_cast<PanTreeStore *>(model));
  Row const &row_a(*static_cast<Row const *>(store->get_row(iter_a)));
  Row const &row_b(*static_cast<Row const *>(store->get_row(iter_b)));

  int sortcol;
  GtkSortType order;
  store->get_sort_column_id(sortcol, order);

  bool const is_root(store->is_root(iter_a));
  if (! is_root)
  {
    sortcol = COL_DATE;
  }

  switch (sortcol)
  {
    case COL_STATE:
      ret = row_a.state - row_b.state;
      break;

    case COL_ACTION:
      // const int a_action (store->get_cell_int (iter_a, COL_ACTION));
      // const int b_action (store->get_cell_int (iter_b, COL_ACTION));
      // ret = a_action - b_action;
      break;

    case COL_SUBJECT:
      ret = strcmp(row_a.get_collated_subject(), row_b.get_collated_subject());
      break;

    case COL_SCORE:
      ret = row_a.article->score - row_b.article->score;
      break;

    case COL_BYTES:
    {
      unsigned long const a_bytes(row_a.article->get_byte_count());
      unsigned long const b_bytes(row_b.article->get_byte_count());
      if (a_bytes < b_bytes)
      {
        ret = -1;
      }
      else if (a_bytes > b_bytes)
      {
        ret = 1;
      }
      else
      {
        ret = 0;
      }
      break;
    }

    case COL_LINES:
    {
      unsigned long const a_lines(row_a.article->get_line_count());
      unsigned long const b_lines(row_b.article->get_line_count());
      if (a_lines < b_lines)
      {
        ret = -1;
      }
      else if (a_lines > b_lines)
      {
        ret = 1;
      }
      else
      {
        ret = 0;
      }
      break;
    }

    case COL_SHORT_AUTHOR:
      ret = strcmp(row_a.get_collated_author(), row_b.get_collated_author());
      break;

    default:
    { // COL_DATE
      const time_t a_time(row_a.article->time_posted);
      const time_t b_time(row_b.article->time_posted);
      if (a_time < b_time)
      {
        ret = -1;
      }
      else if (a_time > b_time)
      {
        ret = 1;
      }
      else
      {
        ret = 0;
      }
      break;
    }
  }

  // we _always_ want the lower levels to be sorted by date
  if (! is_root && order == GTK_SORT_DESCENDING)
  {
    ret = -ret;
  }

  return ret;
}

void HeaderPane ::sort_column_changed_cb(GtkTreeSortable *sortable,
                                         gpointer user_data)
{

  HeaderPane *self(static_cast<HeaderPane *>(user_data));

  const articles_set old_selection(self->get_full_selection());

  if (! old_selection.empty())
  {
    self->select_message_id((*(old_selection.begin()))->message_id);
  }
}

PanTreeStore *HeaderPane ::build_model(Quark const &group,
                                       Data::ArticleTree *atree,
                                       TextMatch const *)
{
  PanTreeStore *store =
    PanTreeStore ::new_tree(N_COLUMNS,
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

  GtkTreeSortable *sort = GTK_TREE_SORTABLE(store);
  for (int i = 0; i < N_COLUMNS; ++i)
  {
    gtk_tree_sortable_set_sort_func(
      sort, i, column_compare_func, GINT_TO_POINTER(i), nullptr);
  }
  if (! group.empty())
  {
    const EvolutionDateMaker date_maker;
    bool const do_thread(_prefs.get_flag("thread-headers", true));
    add_children_to_model(
      store, nullptr, Quark(), atree, date_maker, do_thread);
  }

  return store;
}

namespace {
void save_sort_order(Quark const &group, GroupPrefs &prefs, PanTreeStore *store)
{
  g_assert(store);
  g_assert(GTK_IS_TREE_SORTABLE(store));

  gint sort_column(0);
  GtkSortType sort_type;
  gtk_tree_sortable_get_sort_column_id(
    GTK_TREE_SORTABLE(store), &sort_column, &sort_type);
  prefs.set_int(group, "header-pane-sort-column", sort_column);
  prefs.set_flag(
    group, "header-pane-sort-ascending", sort_type == GTK_SORT_ASCENDING);
}
} // namespace

void HeaderPane ::rebuild()
{

  quarks_t selectme;
  if (1)
  {
    const articles_set old_selection(get_full_selection());
    foreach_const (articles_set, old_selection, it)
    {
      selectme.insert((*it)->message_id);
    }
  }

  _mid_to_row.clear();
  _tree_store = build_model(_group, _atree, nullptr);

  bool const sort_ascending =
    _group_prefs.get_flag(_group, "header-pane-sort-ascending", false);
  int sort_column =
    _group_prefs.get_int(_group, "header-pane-sort-column", COL_DATE);
  if (sort_column < 0
      || sort_column >= N_COLUMNS) // safeguard against odd settings
  {
    sort_column = COL_DATE;
  }
  gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(_tree_store),
                                       sort_column,
                                       sort_ascending ? GTK_SORT_ASCENDING :
                                                        GTK_SORT_DESCENDING);

  GtkTreeModel *model(GTK_TREE_MODEL(_tree_store));
  GtkTreeView *view(GTK_TREE_VIEW(_tree_view));
  gtk_tree_view_set_model(view, model);
  g_object_unref(G_OBJECT(_tree_store)); // store is deleted w/view

  // add signal here to avoid loop
  g_signal_connect(GTK_TREE_SORTABLE(_tree_store),
                   "sort-column-changed",
                   G_CALLBACK(sort_column_changed_cb),
                   this);

  if (_prefs.get_flag("expand-threads-when-entering-group", false))
  {
    gtk_tree_view_expand_all(view);
  }

  if (! selectme.empty())
  {
    select_message_id(*selectme.begin());
  }
}

bool HeaderPane ::set_group(Quark const &new_group)
{

  set_cleared(new_group.empty());

  const Quark old_group(_group);
  bool change(old_group != new_group);

  if (change)
  {
    if (! _group.empty()
        && _prefs.get_flag("mark-group-read-when-leaving-group", false))
    {
      _data.mark_group_read(_group);
    }

    if (_atree)
    {
      _atree->remove_listener(this);
    }

    if (_tree_store)
    {
      save_sort_order(get_group(), _group_prefs, _tree_store);
      _mid_to_row.clear();
      _tree_store = nullptr;
      gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), nullptr);
    }

    _group = new_group;

    delete _atree;
    _atree = nullptr;

    char *pch = g_build_filename(g_get_home_dir(), "News", nullptr);
    Quark path(_group_prefs.get_string(_group, "default-group-save-path", pch));
    g_free(pch);

    if (! _group.empty())
    {
      _atree = _data.group_get_articles(
        new_group, path, _show_type, &_filter, &_rules);
      _atree->add_listener(this);

      rebuild();
      if (gtk_widget_get_realized(_tree_view))
      {
        gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(_tree_view), 0, 0);
      }
    }
  }

  return change;
}

/****
*****
****/

void HeaderPane ::rebuild_article_state(Quark const &message_id)
{
  Row *row(get_row(message_id));
  Article const *article(row->article);
  int const is_read(_data.is_read(article));
  int const state(get_article_state(_data, article));
  bool const changed((state != row->state) || (is_read != row->is_read));
  row->state = state;
  row->is_read = is_read;
  if (changed)
  {
    _tree_store->row_changed(row);
  }
}

void HeaderPane ::rebuild_all_article_states()
{
  foreach (mid_to_row_t, _mid_to_row, it)
  {
    rebuild_article_state((*it)->article->message_id);
  }
}

void HeaderPane ::on_group_read(Quark const &group)
{
  if (group == _group)
  {
    rebuild_all_article_states();
    gtk_widget_queue_draw(_tree_view);
  }
}

/****
*****
****/

namespace {
struct ArticleIsNotInSet : public ArticleTester
{
    quarks_t const &mids;

    ArticleIsNotInSet(quarks_t const &m) :
      mids(m)
    {
    }

    virtual ~ArticleIsNotInSet()
    {
    }

    virtual bool operator()(Article const &a) const override
    {
      return ! mids.count(a.message_id);
    }
};

struct RememberMessageId : public RowActionFunctor
{
    quarks_t &mids;

    RememberMessageId(quarks_t &m) :
      mids(m)
    {
    }

    virtual ~RememberMessageId()
    {
    }

    void operator()(GtkTreeModel *,
                    GtkTreeIter *,
                    Article const &article) override
    {
      mids.insert(article.message_id);
    }
};
} // namespace

void HeaderPane ::collapse_selected()
{
  {
    // get a list of paths
    GtkTreeModel *model(nullptr);
    GtkTreeView *view(GTK_TREE_VIEW(_tree_view));
    GtkTreeSelection *selection(gtk_tree_view_get_selection(view));
    GList *list(gtk_tree_selection_get_selected_rows(selection, &model));
    if (list)
    {
      GtkTreePath *path(static_cast<GtkTreePath *>(list->data));
      gtk_tree_view_collapse_row(view, path);
      //    gtk_tree_view_expand_to_path (view, path);
    }
    g_list_foreach(list, (GFunc)gtk_tree_path_free, nullptr);
    g_list_free(list);
  }
}

void HeaderPane ::select_message_id(Quark const &mid, bool do_scroll)
{

  HeaderPane::Row *row = get_row(mid);
  GtkTreePath *path(_tree_store->get_path(row));
  GtkTreeView *view(GTK_TREE_VIEW(_tree_view));
  bool const expand(_prefs.get_flag("expand-selected-articles", false));
  if (expand)
  {
    gtk_tree_view_expand_to_path(view, path);
  }
  GtkTreeSelection *sel(gtk_tree_view_get_selection(view));
  gtk_tree_selection_select_path(sel, path);
  if (do_scroll)
  {
    gtk_tree_view_set_cursor(view, path, nullptr, false);
    gtk_tree_view_scroll_to_cell(view, path, nullptr, true, 0.5f, 0.0f);
  }
  gtk_tree_path_free(path);
}

void HeaderPane ::on_tree_change(Data::ArticleTree::Diffs const &diffs)
{
  pan_debug(diffs.added.size()
            << " article added; " << diffs.reparented.size() << " reparented; "
            << diffs.removed.size() << " removed");

  // we might need to change the selection after the update.
  const article_v old_selection(get_full_selection_v());
  quarks_t new_selection;
  foreach_const (article_v, old_selection, it)
  {
    if (! diffs.removed.count((*it)->message_id))
    {
      new_selection.insert((*it)->message_id);
    }
  }

  // if the old selection survived,
  // is it visible on the screen?
  bool selection_was_visible(true);
  if (! new_selection.empty())
  {
    GtkTreeView *view(GTK_TREE_VIEW(_tree_view));
    Row *row(get_row(*new_selection.begin()));
    GtkTreePath *a(nullptr), *b(nullptr), *p(_tree_store->get_path(row));
    gtk_tree_view_get_visible_range(view, &a, &b);
    selection_was_visible =
      (gtk_tree_path_compare(a, p) <= 0 && gtk_tree_path_compare(p, b) <= 0);
    gtk_tree_path_free(a);
    gtk_tree_path_free(b);
    gtk_tree_path_free(p);
  }

  // if none of the current selection survived,
  // we need to select something to replace the
  // current selection.
  if (! old_selection.empty() && new_selection.empty())
  {
    ArticleIsNotInSet tester(diffs.removed);
    RememberMessageId actor(new_selection);
    action_next_if(tester, actor);
  }

  // added...
  bool const do_thread(_prefs.get_flag("thread-headers", true));
  if (! diffs.added.empty())
  {
    const EvolutionDateMaker date_maker;
    PanTreeStore::parent_to_children_t tmp;
    foreach_const (Data::ArticleTree::Diffs::added_t, diffs.added, it)
    {
      create_row(date_maker, _atree->get_article(it->first));
    }
    foreach_const (Data::ArticleTree::Diffs::added_t, diffs.added, it)
    {
      Row *parent(do_thread ? get_row(it->second.parent) : nullptr);
      Row *child(get_row(it->first));
      tmp[parent].push_back(child);
    }

    g_object_ref(G_OBJECT(_tree_store));
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), nullptr);
    _tree_store->insert_sorted(tmp);
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view),
                            GTK_TREE_MODEL(_tree_store));
    g_object_unref(G_OBJECT(_tree_store));
  }

  // reparent...
  if (do_thread && ! diffs.reparented.empty())
  {
    PanTreeStore::parent_to_children_t tmp;
    foreach_const (Data::ArticleTree::Diffs::reparented_t, diffs.reparented, it)
    {
      Row *parent(get_row(it->second.new_parent));
      Row *child(get_row(it->second.message_id));
      tmp[parent].push_back(child);
    }
    _tree_store->reparent(tmp);
  }

  // removed...
  if (! diffs.removed.empty())
  {
    RowLessThan o;
    std::vector<Row *> keep;
    PanTreeStore::rows_t kill;
    std::set_difference(_mid_to_row.begin(),
                        _mid_to_row.end(),
                        diffs.removed.begin(),
                        diffs.removed.end(),
                        inserter(keep, keep.begin()),
                        o);
    std::set_difference(_mid_to_row.begin(),
                        _mid_to_row.end(),
                        keep.begin(),
                        keep.end(),
                        inserter(kill, kill.begin()),
                        o);
    g_assert(keep.size() + kill.size() == _mid_to_row.size());

    g_object_ref(G_OBJECT(_tree_store));
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view), nullptr);
    _tree_store->remove(kill);
    gtk_tree_view_set_model(GTK_TREE_VIEW(_tree_view),
                            GTK_TREE_MODEL(_tree_store));
    g_object_unref(G_OBJECT(_tree_store));
    _mid_to_row.get_container().swap(keep);
  }

  // changed...
  foreach_const (quarks_t, diffs.changed, it)
  {
    rebuild_article_state(*it);
  }

  if (! diffs.added.empty()
      && _prefs.get_flag("expand-threads-when-entering-group", false))
  {
    gtk_tree_view_expand_all(GTK_TREE_VIEW(_tree_view));
  }

  // update our selection if necessary.
  // if the new selection has just been added or reparented,
  // and it was visible on the screen before,
  // then scroll to ensure it's still visible.
  if (! new_selection.empty())
  {
    bool const do_scroll =
      selection_was_visible
      && (! diffs.added.empty() || ! diffs.reparented.empty()
          || ! diffs.removed.empty());
    select_message_id(*new_selection.begin(), do_scroll);
  }
}

/****
*****  SELECTION
****/

Article const *HeaderPane ::get_first_selected_article() const
{
  Article const *a(nullptr);
  const std::set<Article const *> articles(get_full_selection());
  if (! articles.empty())
  {
    a = *articles.begin();
  }
  return a;
}

Article *HeaderPane ::get_first_selected_article()
{
  Article *a(nullptr);
  std::set<Article const *> articles(get_full_selection());
  if (! articles.empty())
  {
    a = (Article *)*articles.begin();
  }
  return a;
}

const guint HeaderPane ::get_full_selection_rows_num() const
{
  return (gtk_tree_selection_count_selected_rows(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(_tree_view))));
}

void HeaderPane ::get_full_selection_v_foreach(GtkTreeModel *model,
                                               GtkTreePath *,
                                               GtkTreeIter *iter,
                                               gpointer data)
{
  static_cast<article_v *>(data)->push_back(get_article(model, iter));
}

std::vector<Article const *> HeaderPane ::get_full_selection_v() const
{
  std::vector<Article const *> articles;
  GtkTreeView *view(GTK_TREE_VIEW(_tree_view));
  gtk_tree_selection_selected_foreach(
    gtk_tree_view_get_selection(view), get_full_selection_v_foreach, &articles);
  return articles;
}

std::set<Article const *> HeaderPane ::get_full_selection() const
{
  std::set<Article const *> articles;

  const std::vector<Article const *> articles_v(get_full_selection_v());
  foreach_const (std::vector<Article const *>, articles_v, it)
  {
    articles.insert(*it);
  }

  return articles;
}

void HeaderPane ::mark_all_flagged()
{

  GtkTreeIter iter;
  GtkTreeModel *model(gtk_tree_view_get_model(GTK_TREE_VIEW(_tree_view)));
  GtkTreeSelection *sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(_tree_view)));
  gtk_tree_selection_unselect_all(sel);
  gtk_tree_model_get_iter_first(model, &iter);
  walk_and_collect_flagged(model, &iter, sel);
}

void HeaderPane ::invert_selection()
{

  GtkTreeIter iter;
  GtkTreeModel *model(gtk_tree_view_get_model(GTK_TREE_VIEW(_tree_view)));
  GtkTreeSelection *sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(_tree_view)));
  gtk_tree_model_get_iter_first(model, &iter);
  walk_and_invert_selection(model, &iter, sel);
}

void HeaderPane ::walk_and_invert_selection(GtkTreeModel *model,
                                            GtkTreeIter *cur,
                                            GtkTreeSelection *ref) const
{
  for (;;)
  {
    bool const selected(gtk_tree_selection_iter_is_selected(ref, cur));
    if (selected)
    {
      gtk_tree_selection_unselect_iter(ref, cur);
    }
    else
    {
      gtk_tree_selection_select_iter(ref, cur);
    }
    GtkTreeIter child;
    if (gtk_tree_model_iter_children(model, &child, cur))
    {
      walk_and_invert_selection(model, &child, ref);
    }
    if (! gtk_tree_model_iter_next(model, cur))
    {
      break;
    }
  }
}

void HeaderPane ::walk_and_collect_flagged(GtkTreeModel *model,
                                           GtkTreeIter *cur,
                                           GtkTreeSelection *setme) const
{
  for (;;)
  {
    Article const *a(get_article(model, cur));
    if (a->get_flag())
    {
      gtk_tree_selection_select_iter(setme, cur);
    }
    GtkTreeIter child;
    if (gtk_tree_model_iter_children(model, &child, cur))
    {
      walk_and_collect_flagged(model, &child, setme);
    }
    if (! gtk_tree_model_iter_next(model, cur))
    {
      break;
    }
  }
}

void HeaderPane ::walk_and_collect(GtkTreeModel *model,
                                   GtkTreeIter *cur,
                                   articles_set &setme) const
{
  for (;;)
  {
    setme.insert(get_article(model, cur));
    GtkTreeIter child;
    if (gtk_tree_model_iter_children(model, &child, cur))
    {
      walk_and_collect(model, &child, setme);
    }
    if (! gtk_tree_model_iter_next(model, cur))
    {
      break;
    }
  }
}

namespace {
struct NestedData
{
    HeaderPane const *pane;
    articles_t articles;
    bool mark_all; /* for mark_article_(un)read and mark_thread_(un)read */
};
} // namespace

void HeaderPane ::get_nested_foreach(GtkTreeModel *model,
                                     GtkTreePath *path,
                                     GtkTreeIter *iter,
                                     gpointer data) const
{
  NestedData &ndata(*static_cast<NestedData *>(data));
  articles_set &articles(ndata.articles);
  articles.insert(get_article(model, iter));
  bool const expanded(
    gtk_tree_view_row_expanded(GTK_TREE_VIEW(_tree_view), path));
  GtkTreeIter child;
  if ((! expanded || ndata.mark_all)
      && gtk_tree_model_iter_children(model, &child, iter))
  {
    walk_and_collect(model, &child, articles);
  }
}

void HeaderPane ::get_nested_foreach_static(GtkTreeModel *model,
                                            GtkTreePath *path,
                                            GtkTreeIter *iter,
                                            gpointer data)
{
  NestedData &ndata(*static_cast<NestedData *>(data));
  ndata.pane->get_nested_foreach(model, path, iter, &ndata);
}

articles_set HeaderPane ::get_nested_selection(bool do_mark_all) const
{
  NestedData data;
  data.pane = this;
  data.mark_all = do_mark_all;
  GtkTreeSelection *selection(
    gtk_tree_view_get_selection(GTK_TREE_VIEW(_tree_view)));
  gtk_tree_selection_selected_foreach(
    selection, get_nested_foreach_static, &data);
  return data.articles;
}

/****
*****  POPUP MENU
****/

void HeaderPane ::do_popup_menu(GtkWidget *,
                                GdkEventButton *event,
                                gpointer pane_g)
{
  HeaderPane *self(static_cast<HeaderPane *>(pane_g));
  GtkWidget *menu(
    self->_action_manager.get_action_widget("/header-pane-popup"));
  gtk_menu_popup(GTK_MENU(menu),
                 nullptr,
                 nullptr,
                 nullptr,
                 nullptr,
                 (event ? event->button : 0),
                 (event ? event->time : 0));
}

namespace {
gboolean on_popup_menu(GtkWidget *treeview, gpointer userdata)
{
  HeaderPane::do_popup_menu(treeview, nullptr, userdata);
  return true;
}
} // namespace

namespace {
bool row_collapsed_or_expanded(false);

void row_collapsed_cb(GtkTreeView *, GtkTreeIter *, GtkTreePath *, gpointer)
{
  row_collapsed_or_expanded = true;
}

void row_expanded_cb(GtkTreeView *view,
                     GtkTreeIter *,
                     GtkTreePath *path,
                     gpointer)
{
  row_collapsed_or_expanded = true;
  gtk_tree_view_expand_row(view, path, true);
}

struct Blah
{
    GtkTreeView *view;
    GtkTreePath *path;
    GtkTreeViewColumn *col;
};

gboolean maybe_activate_on_idle_idle(gpointer blah_gpointer)
{
  Blah *blah = (Blah *)blah_gpointer;
  if (! row_collapsed_or_expanded)
  {
    gtk_tree_view_row_activated(blah->view, blah->path, blah->col);
  }
  gtk_tree_path_free(blah->path);
  g_free(blah);
  return false;
}

/**
 * There doesn't seem to be any way to see if a mouse click in a tree view
 * happened on the expander or elsewhere in a row, so when deciding whether or
 * not to activate a row on single click, let's wait and see if a row expands or
 * collapses.
 */
void maybe_activate_on_idle(GtkTreeView *view,
                            GtkTreePath *path,
                            GtkTreeViewColumn *col)
{
  row_collapsed_or_expanded = false;
  Blah *blah = (Blah *)g_new(Blah, 1);
  blah->view = view;
  blah->path = path;
  blah->col = col;
  g_idle_add(maybe_activate_on_idle_idle, blah);
}
} // namespace

namespace {
static gboolean return_pressed_download_all(gpointer data)
{
  HeaderPane *self(static_cast<HeaderPane *>(data));
  // TODO move this into headerpane
  self->_gui.do_read_or_save_articles();
  return false;
}

static gboolean left_pressed(gpointer data)
{
  HeaderPane *self(static_cast<HeaderPane *>(data));
  // TODO move this into headerpane
  self->_gui.do_collapse_thread();
  return false;
}

static gboolean right_pressed(gpointer data)
{
  HeaderPane *self(static_cast<HeaderPane *>(data));
  // TODO move this into headerpane
  self->_gui.do_expand_thread();
  return false;
}
} // namespace

// TODO just pass TRUE and let actions.cc handle that!
gboolean HeaderPane ::on_keyboard_button_pressed(GtkWidget *widget,
                                                 GdkEventKey *event,
                                                 gpointer data)
{
  if (event->type == GDK_KEY_PRESS)
  {
    if (event->keyval == GDK_KEY_Return)
    {
      g_idle_add(return_pressed_download_all, data);
      return TRUE;
    }
    if (event->keyval == GDK_KEY_Left)
    {
      g_idle_add(left_pressed, data);
      return TRUE;
    }
    if (event->keyval == GDK_KEY_Right)
    {
      g_idle_add(right_pressed, data);
      return TRUE;
    }
  }

  return FALSE;
}

gboolean HeaderPane ::on_button_pressed(GtkWidget *treeview,
                                        GdkEventButton *event,
                                        gpointer userdata)
{
  HeaderPane *self(static_cast<HeaderPane *>(userdata));
  GtkTreeView *tv(GTK_TREE_VIEW(treeview));

  // single click with the right mouse button?
  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
  {
    GtkTreeSelection *selection = gtk_tree_view_get_selection(tv);
    GtkTreePath *path;
    if (gtk_tree_view_get_path_at_pos(
          tv, (gint)event->x, (gint)event->y, &path, nullptr, nullptr, nullptr))
    {
      if (! gtk_tree_selection_path_is_selected(selection, path))
      {
        gtk_tree_selection_unselect_all(selection);
        gtk_tree_selection_select_path(selection, path);
      }
      gtk_tree_path_free(path);
    }
    HeaderPane::do_popup_menu(treeview, event, userdata);
    return true;
  }
  else if (self->_prefs.get_flag("single-click-activates-article", true)
           && (event->type == GDK_BUTTON_RELEASE) && (event->button == 1)
           && (event->send_event == false)
           && (event->window == gtk_tree_view_get_bin_window(tv))
           && ! (event->state
                 & (GDK_SHIFT_MASK | GDK_CONTROL_MASK | GDK_MOD1_MASK))
           && (self->get_full_selection_rows_num() == 1u))
  {
    GtkTreePath *path;
    GtkTreeViewColumn *col;
    gint cell_x(0), cell_y(0);
    if (gtk_tree_view_get_path_at_pos(
          tv, (gint)event->x, (gint)event->y, &path, &col, &cell_x, &cell_y))
    {
      maybe_activate_on_idle(tv, path, col);
    }
  }
  else if ((event->type == GDK_BUTTON_RELEASE) && (event->button == 2)
           && (event->send_event == false)
           && (event->window == gtk_tree_view_get_bin_window(tv)))
  {
    self->_action_manager.activate_action("clear-body-pane");
  }

  return false;
}

namespace {
bool has_image_type_in_subject(Article const &a)
{
  const StringView s(a.subject.to_view());
  return s.strstr(".jpg") || s.strstr(".JPG") || s.strstr(".gif")
         || s.strstr(".GIF") || s.strstr(".jpeg") || s.strstr(".JPEG")
         || s.strstr(".png") || s.strstr(".PNG");
}

gboolean on_row_activated_idle(gpointer pane_g)
{
  HeaderPane *pane(static_cast<HeaderPane *>(pane_g));
  Article const *a(pane->get_first_selected_article());
  if (a)
  {
    const size_t lines = a->get_line_count();
      bool const is_bigish = lines >= 20000;
    bool const image_subject = has_image_type_in_subject(*a);
    bool const is_pictures_newsgroup =
      pane->get_group().to_view().strstr("pictures") != nullptr;
    // This is not the number of MIME parts
    int const part_count = a->get_found_part_count();

      // update body pane with article information
      pane->_action_manager.activate_action ("read-selected-article");

      if (is_bigish && ! is_pictures_newsgroup && ! image_subject
          && part_count > 1)
      {
        pane->_action_manager.activate_action("save-articles");
      }
    }
  return false;
}
} // namespace

void HeaderPane ::on_row_activated(GtkTreeView *,
                                   GtkTreePath *,
                                   GtkTreeViewColumn *,
                                   gpointer pane_g)
{
  g_idle_add(on_row_activated_idle, pane_g);
}

/****
*****
*****  FILTERS
*****
****/

namespace {
char const *mode_strings[] = {
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

} // namespace

#define RANGE 4998

std::pair<int, int> HeaderPane ::get_int_from_rules_str(std::string val)
{
  std::pair<int, int> res;
  if (val == "new")
  {
    res.first = 0;
    res.second = 0;
  }
  if (val == "never")
  {
    res.first = 10;
    res.second = 5;
  } // inversed, so never true
  if (val == "watched")
  {
    res.first = 9999;
    res.second = 999999;
  }
  if (val == "high")
  {
    res.first = 5000;
    res.second = 5000 + RANGE;
  }
  if (val == "medium")
  {
    res.first = 1;
    res.second = 1 + RANGE;
  }
  if (val == "low")
  {
    res.first = -9998;
    res.second = -1;
  }
  if (val == "ignored")
  {
    res.first = -999999;
    res.second = -9999;
  }
  return res;
}

void HeaderPane ::rebuild_rules(bool enable)
{

  if (! enable)
  {
    _rules.clear();
    return;
  }

  RulesInfo &r(_rules);
  r.set_type_aggregate();
  RulesInfo *tmp;

  std::pair<int, int> res;

  res =
    get_int_from_rules_str(_prefs.get_string("rules-delete-value", "never"));
  tmp = new RulesInfo;
  tmp->set_type_delete_b(res.first, res.second);
  r._aggregates.push_back(tmp);

  res =
    get_int_from_rules_str(_prefs.get_string("rules-mark-read-value", "never"));
  tmp = new RulesInfo;
  tmp->set_type_mark_read_b(res.first, res.second);
  r._aggregates.push_back(tmp);

  res =
    get_int_from_rules_str(_prefs.get_string("rules-autocache-value", "never"));
  tmp = new RulesInfo;
  tmp->set_type_autocache_b(res.first, res.second);
  r._aggregates.push_back(tmp);

  res =
    get_int_from_rules_str(_prefs.get_string("rules-auto-dl-value", "never"));
  tmp = new RulesInfo;
  tmp->set_type_dl_b(res.first, res.second);
  r._aggregates.push_back(tmp);
}

void HeaderPane ::rebuild_filter(std::string const &text, int mode)
{

  TextMatch::Description d;
  d.negate = false;
  d.case_sensitive = false;
  d.type = TextMatch::CONTAINS;
  d.text = text;

  FilterInfo &f(_filter);

  f.set_type_aggregate_and();

  // entry field filter...
  if (! text.empty())
  {
    FilterInfo *entry_filter = new FilterInfo;
    if (mode == SUBJECT)
    {
      entry_filter->set_type_text("Subject", d);
    }
    else if (mode == AUTHOR)
    {
      entry_filter->set_type_text("From", d);
    }
    else if (mode == MESSAGE_ID)
    {
      entry_filter->set_type_text("Message-ID", d);
    }
    else if (mode == SUBJECT_OR_AUTHOR)
    {
      FilterInfo *f1 = new FilterInfo, *f2 = new FilterInfo;
      entry_filter->set_type_aggregate_or();
      f1->set_type_text("Subject", d);
      f2->set_type_text("From", d);
      entry_filter->_aggregates.push_back(f1);
      entry_filter->_aggregates.push_back(f2);
    }
    else if (mode == SUBJECT_OR_AUTHOR_REGEX)
    {
      FilterInfo *f1 = new FilterInfo, *f2 = new FilterInfo;
      entry_filter->set_type_aggregate_or();
      d.type = TextMatch::REGEX;
      f1->set_type_text("Subject", d);
      f2->set_type_text("From", d);
      entry_filter->_aggregates.push_back(f1);
      entry_filter->_aggregates.push_back(f2);
    }
    f._aggregates.push_back(entry_filter);
  }

  if (_action_manager.is_action_active("match-only-read-articles"))
  {
    // std::cerr << LINE_ID << " AND is read" << std::endl;
    FilterInfo *tmp = new FilterInfo;
    tmp->set_type_is_read();
    f._aggregates.push_back(tmp);
  }
  if (_action_manager.is_action_active("match-only-unread-articles"))
  {
    // std::cerr << LINE_ID << " AND is unread" << std::endl;
    FilterInfo *tmp = new FilterInfo;
    tmp->set_type_is_unread();
    f._aggregates.push_back(tmp);
  }
  if (_action_manager.is_action_active("match-only-cached-articles"))
  {
    // std::cerr << LINE_ID << " AND is cached" << std::endl;
    FilterInfo *tmp = new FilterInfo;
    tmp->set_type_cached();
    f._aggregates.push_back(tmp);
  }
  if (_action_manager.is_action_active("match-only-binary-articles"))
  {
    // std::cerr << LINE_ID << " AND has an attachment" << std::endl;
    FilterInfo *tmp = new FilterInfo;
    tmp->set_type_binary();
    f._aggregates.push_back(tmp);
  }
  if (_action_manager.is_action_active("match-only-my-articles"))
  {
    // std::cerr << LINE_ID << " AND was posted by me" << std::endl;
    FilterInfo *tmp = new FilterInfo;
    tmp->set_type_posted_by_me();
    f._aggregates.push_back(tmp);
  }

  // try to fold the six ranges into as few FilterInfo items as possible..
  typedef std::pair<int, int> range_t;
  std::vector<range_t> ranges;
  ranges.reserve(6);
  if (_action_manager.is_action_active("match-only-watched-articles"))
  {
    ranges.push_back(range_t(9999, INT_MAX));
  }
  else
  {
    if (_action_manager.is_action_active("match-ignored-articles"))
    {
      ranges.push_back(range_t(INT_MIN, -9999));
    }
    if (_action_manager.is_action_active("match-low-scoring-articles"))
    {
      ranges.push_back(range_t(-9998, -1));
    }
    if (_action_manager.is_action_active("match-normal-scoring-articles"))
    {
      ranges.push_back(range_t(0, 0));
    }
    if (_action_manager.is_action_active("match-medium-scoring-articles"))
    {
      ranges.push_back(range_t(1, 4999));
    }
    if (_action_manager.is_action_active("match-high-scoring-articles"))
    {
      ranges.push_back(range_t(5000, 9998));
    }
    if (_action_manager.is_action_active("match-watched-articles"))
    {
      ranges.push_back(range_t(9999, INT_MAX));
    }
  }
  for (size_t i = 0; ! ranges.empty() && i < ranges.size() - 1;)
  {
    if (ranges[i].second + 1 != ranges[i + 1].first)
    {
      ++i;
    }
    else
    {
      ranges[i].second = ranges[i + 1].second;
      ranges.erase(ranges.begin() + i + 1);
    }
  }

  // for (size_t i=0; i<ranges.size(); ++i) std::cerr << LINE_ID << " range ["
  // << ranges[i].first << "..." << ranges[i].second << "]" << std::endl;

  std::deque<FilterInfo *> filters;
  for (size_t i = 0; i < ranges.size(); ++i)
  {
    range_t const &range(ranges[i]);
    bool const low_bound(range.first == INT_MIN);
    bool const hi_bound(range.second == INT_MAX);
    if (low_bound && hi_bound)
    {
      // everything matches -- do nothing
    }
    else if (hi_bound)
    {
      FilterInfo *tmp = new FilterInfo;
      tmp->set_type_score_ge(range.first);
      // std::cerr << LINE_ID << " AND has a score >= " << range.first <<
      // std::endl;
      filters.push_back(tmp);
    }
    else if (low_bound)
    {
      FilterInfo *tmp = new FilterInfo;
      tmp->set_type_score_le(range.second);
      // std::cerr << LINE_ID << " AND has a score <= " << range.second <<
      // std::endl;
      filters.push_back(tmp);
    }
    else
    { // not bound on either side; need an aggregate
      FilterInfo *tmp = new FilterInfo;
      FilterInfo *s = new FilterInfo;
      s->set_type_aggregate_and();
      tmp->set_type_score_ge(range.first);
      s->_aggregates.push_back(tmp);
      tmp->set_type_score_le(range.second);
      s->_aggregates.push_back(tmp);
      // std::cerr << LINE_ID << " AND has a in [" << range.first << "..." <<
      // range.second << ']' << std::endl;
      filters.push_back(s);
    }
  }
  if (filters.size() == 1) // can fit in an `and' parent
  {
    f._aggregates.push_back(filters[0]);
  }
  else if (! filters.empty())
  { // needs an `or' parent
    FilterInfo *s = new FilterInfo;
    s->set_type_aggregate_or();
    s->_aggregates.swap(filters);
    f._aggregates.push_back(s);
  }
  // std::cerr << LINE_ID << " number of filters: " << f._aggregates.size() <<
  // std::endl;
}

void HeaderPane ::filter(std::string const &text, int mode)
{
  rebuild_filter(text, mode);

  if (_atree)
  {
    _wait.watch_cursor_on();

    if (_filter._aggregates.empty())
    {
      _atree->set_filter();
    }
    else
    {
      _atree->set_filter(_show_type, &_filter);
    }

    _wait.watch_cursor_off();
  }
}

void HeaderPane ::rules(bool enable)
{
  rebuild_rules(enable);

  if (_atree)
  {
    _wait.watch_cursor_on();

    if (_rules._aggregates.empty())
    {
      _atree->set_rules();
    }
    else
    {
      _atree->set_rules(&_rules);
    }

    _wait.watch_cursor_off();
  }
}

namespace {
// the text typed by the user.
std::string search_text;

guint entry_changed_tag(0u);
guint activate_soon_tag(0u);

// AUTHOR, SUBJECT, SUBJECT_OR_AUTHOR, or MESSAGE_ID
int mode;

void set_search_entry(GtkWidget *entry, char const *s)
{
  g_signal_handler_block(entry, entry_changed_tag);
  gtk_entry_set_text(GTK_ENTRY(entry), s);
  g_signal_handler_unblock(entry, entry_changed_tag);
}

gboolean search_entry_focus_in_cb(GtkWidget *w, GdkEventFocus *, gpointer)
{
  gtk_widget_override_color(w, GTK_STATE_FLAG_NORMAL, nullptr);
  set_search_entry(w, search_text.c_str());
  return false;
}

void refresh_search_entry(GtkWidget *w)
{
  if (search_text.empty() && ! gtk_widget_has_focus(w))
  {
    GdkRGBA c;
    gdk_rgba_parse(&c, "0xAAA");
    gtk_widget_override_color(w, GTK_STATE_FLAG_NORMAL, &c);
    set_search_entry(w, _(mode_strings[mode]));
  }
}

gboolean search_entry_focus_out_cb(GtkWidget *w, GdkEventFocus *, gpointer)
{
  refresh_search_entry(w);
  return false;
}

void search_activate(HeaderPane *h)
{
  h->filter(search_text, mode);
}

void remove_activate_soon_tag()
{
  if (activate_soon_tag != 0)
  {
    g_source_remove(activate_soon_tag);
    activate_soon_tag = 0;
  }
}

void search_entry_activated(GtkEntry *, gpointer h_gpointer)
{
  search_activate(static_cast<HeaderPane *>(h_gpointer));
  remove_activate_soon_tag();
}

gboolean activated_timeout_cb(gpointer h_gpointer)
{
  search_activate(static_cast<HeaderPane *>(h_gpointer));
  remove_activate_soon_tag();
  return false; // remove the source
}

// ensure there's exactly one activation timeout
// and that it's set to go off in a half second from now.
void bump_activate_soon_tag(HeaderPane *h)
{
  remove_activate_soon_tag();
  activate_soon_tag = g_timeout_add(500, activated_timeout_cb, h);
}

// when the user changes the filter text,
// update our state variable and bump the activate timeout.
void search_entry_changed(GtkEditable *e, gpointer h_gpointer)
{
  search_text = gtk_entry_get_text(GTK_ENTRY(e));
  bump_activate_soon_tag(static_cast<HeaderPane *>(h_gpointer));
  refresh_search_entry(GTK_WIDGET(e));
}

// when the search mode is changed via the menu,
// update our state variable and bump the activate timeout.
void search_menu_toggled_cb(GtkCheckMenuItem *menu_item, gpointer entry_g)
{
  if (gtk_check_menu_item_get_active(menu_item))
  {
    mode = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(menu_item), "MODE"));
    refresh_search_entry(GTK_WIDGET(entry_g));
    HeaderPane *h =
      (HeaderPane *)g_object_get_data(G_OBJECT(entry_g), "header-pane");
    bump_activate_soon_tag(h);
  }
}

void entry_icon_release(GtkEntry *,
                        GtkEntryIconPosition icon_pos,
                        GdkEventButton *,
                        gpointer menu)
{
  if (icon_pos == GTK_ENTRY_ICON_PRIMARY)
  {
    gtk_menu_popup(GTK_MENU(menu),
                   nullptr,
                   nullptr,
                   nullptr,
                   nullptr,
                   0,
                   gtk_get_current_event_time());
  }
}

void entry_icon_release_2(GtkEntry *entry,
                          GtkEntryIconPosition icon_pos,
                          GdkEventButton *,
                          gpointer pane_gpointer)
{
  if (icon_pos == GTK_ENTRY_ICON_SECONDARY)
  {
    set_search_entry(GTK_WIDGET(entry), "");
    refresh_search_entry(GTK_WIDGET(entry));
    search_text.clear();
    search_entry_activated(nullptr, pane_gpointer);
  }
}

void ellipsize_if_supported(GObject *o)
{
  g_object_set(o, "ellipsize", PANGO_ELLIPSIZE_END, nullptr);
}
} // namespace

void HeaderPane::build_tree_columns() {
  GtkTreeView *tree_view(GTK_TREE_VIEW(_tree_view));
  int const xpad(_prefs.get_int("tree-view-row-margins", 1));

  // out with the old columns, if any
  GList *old_columns = gtk_tree_view_get_columns(tree_view);
  for (GList *l = old_columns; l != nullptr; l = l->next) {
    gtk_tree_view_remove_column(tree_view, GTK_TREE_VIEW_COLUMN(l->data));
  }
  g_list_free(old_columns);

  // Column configuration structure
  struct ColumnConfig {
    char const *name;
    char const *title;
    GType renderer_type;
    bool resizable;
    int default_width;
    int sort_column_id;
    GtkTreeCellDataFunc data_func;
    bool right_align;
    bool is_expander;
  };

  // Define all column configurations
  ColumnConfig configs[] = {
      {"state", nullptr, GTK_TYPE_CELL_RENDERER_PIXBUF, false, 24, COL_STATE,
       render_state, false, false},
      {"action", nullptr, GTK_TYPE_CELL_RENDERER_PIXBUF, false, 24, -1,
       render_action, false, false},
      {"subject", _("Subject"), GTK_TYPE_CELL_RENDERER_TEXT, true, 400,
       COL_SUBJECT, render_subject, false, true},
      {"score", _("Score"), GTK_TYPE_CELL_RENDERER_TEXT, true, 50, COL_SCORE,
       render_score, true, false},
      {"author", _("Author"), GTK_TYPE_CELL_RENDERER_TEXT, true, 133,
       COL_SHORT_AUTHOR, render_author, false, false},
      {"lines", _("Lines"), GTK_TYPE_CELL_RENDERER_TEXT, true, 60, COL_LINES,
       render_lines, true, false},
      {"bytes", _("Bytes"), GTK_TYPE_CELL_RENDERER_TEXT, true, 80, COL_BYTES,
       render_bytes, true, false},
      {"date", _("Date"), GTK_TYPE_CELL_RENDERER_TEXT, true, 120, COL_DATE,
       render_date, false, false}};

  // Helper function to create a column
  auto create_column = [&](ColumnConfig const &config) -> GtkTreeViewColumn * {
    std::string const width_key =
        std::string("header-pane-") + config.name + "-column-width";

    GtkCellRenderer *r;
    if (config.renderer_type == GTK_TYPE_CELL_RENDERER_PIXBUF) {
      r = GTK_CELL_RENDERER(
          g_object_new(config.renderer_type, "xpad", xpad, "ypad", 0, nullptr));
    } else {
      if (config.right_align) {
        r = GTK_CELL_RENDERER(g_object_new(config.renderer_type, "xpad", xpad,
                                           "ypad", 0, "xalign", 1.0, nullptr));
      } else {
        r = GTK_CELL_RENDERER(g_object_new(config.renderer_type, "xpad", xpad,
                                           "ypad", 0, nullptr));
      }
      ellipsize_if_supported(G_OBJECT(r));
    }

    GtkTreeViewColumn *col;
    if (config.title) {
      col = gtk_tree_view_column_new_with_attributes(config.title, r, nullptr);
    } else {
      col = gtk_tree_view_column_new();
      gtk_tree_view_column_pack_start(col, r, false);
    }

    gtk_tree_view_column_set_sizing(col, GTK_TREE_VIEW_COLUMN_FIXED);
    gtk_tree_view_column_set_fixed_width(
        col, _prefs.get_int(width_key, config.default_width));
    gtk_tree_view_column_set_resizable(col, config.resizable);

    if (config.sort_column_id != -1) {
      gtk_tree_view_column_set_sort_column_id(col, config.sort_column_id);
    }

    gtk_tree_view_column_set_cell_data_func(col, r, config.data_func, this,
                                            nullptr);

    g_object_set_data_full(G_OBJECT(col), "column-width-key",
                           g_strdup(width_key.c_str()), g_free);

    return col;
  };

  // get the user-configurable column list
  const std::string columns(_prefs.get_string(
      "header-pane-columns", "state,action,subject,score,author,lines,date"));
  StringView v(columns), tok;

  while (v.pop_token(tok, ',')) {
    std::string const &name(tok.to_string());

    // Find the configuration for this column
    for (const auto& config : configs) {
      if (name == config.name) {
        GtkTreeViewColumn *col = create_column(config);
        gtk_tree_view_append_column(tree_view, col);

        if (config.is_expander) {
          gtk_tree_view_set_expander_column(tree_view, col);
        }
        break;
      }
    }
  }
}

void HeaderPane ::refilter()
{
  search_activate(this);
}

void HeaderPane ::set_show_type(const Data::ShowType show_type)
{
  if (show_type != _show_type)
  {
    _show_type = show_type;
    refilter();
  }
}

HeaderPane ::~HeaderPane()
{
  if (_selection_changed_idle_tag)
  {
    g_source_remove(_selection_changed_idle_tag);
    _selection_changed_idle_tag = 0;
  }

  _cache.remove_listener(this);
  _queue.remove_listener(this);
  _prefs.remove_listener(this);
  _data.remove_listener(this);

  // save the column widths
  GList *columns = gtk_tree_view_get_columns(GTK_TREE_VIEW(_tree_view));
  for (GList *l = columns; l != nullptr; l = l->next)
  {
    GtkTreeViewColumn *col(GTK_TREE_VIEW_COLUMN(l->data));
    int const width = gtk_tree_view_column_get_width(col);
    char const *width_key =
      (char const *)g_object_get_data(G_OBJECT(col), "column-width-key");
    _prefs.set_int(width_key, width);
  }
  g_list_free(columns);

  _prefs.set_string("last-visited-group",
                    get_cleared() ? "" : _group.to_view());
  set_group(Quark());

  for (guint i = 0; i < ICON_QTY; ++i)
  {
    g_object_unref(G_OBJECT(_icons[i].pixbuf));
  }
}

GtkWidget *HeaderPane ::create_filter_entry()
{
  GtkWidget *entry = gtk_entry_new();
  _action_manager.disable_accelerators_when_focused(entry);
  g_object_set_data(G_OBJECT(entry), "header-pane", this);
  g_signal_connect(
    entry, "focus-in-event", G_CALLBACK(search_entry_focus_in_cb), nullptr);
  g_signal_connect(
    entry, "focus-out-event", G_CALLBACK(search_entry_focus_out_cb), nullptr);
  g_signal_connect(entry, "activate", G_CALLBACK(search_entry_activated), this);
  entry_changed_tag =
    g_signal_connect(entry, "changed", G_CALLBACK(search_entry_changed), this);

  gtk_entry_set_icon_from_icon_name(
    GTK_ENTRY(entry), GTK_ENTRY_ICON_PRIMARY, "edit-find");
  gtk_entry_set_icon_from_icon_name(
    GTK_ENTRY(entry), GTK_ENTRY_ICON_SECONDARY, "edit-clear");

  bool regex = _prefs.get_flag("use-regex", false);
  GtkWidget *menu = gtk_menu_new();
  if (regex == true)
  {
    mode = 1;
  }
  else
  {
    mode = 0;
  }
  GSList *l = nullptr;
  for (int i = 0, qty = G_N_ELEMENTS(mode_strings); i < qty; ++i)
  {
    GtkWidget *w = gtk_radio_menu_item_new_with_label(l, _(mode_strings[i]));
    l = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(w));
    g_object_set_data(G_OBJECT(w), "MODE", GINT_TO_POINTER(i));
    g_signal_connect(w, "toggled", G_CALLBACK(search_menu_toggled_cb), entry);
    if (mode == i)
    {
      gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(w), TRUE);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), w);
    gtk_widget_show(w);
  }
  g_signal_connect(entry, "icon-release", G_CALLBACK(entry_icon_release), menu);
  g_signal_connect(
    entry, "icon-release", G_CALLBACK(entry_icon_release_2), this);

  refresh_search_entry(entry);

  return entry;
}

void HeaderPane ::on_selection_changed(GtkTreeSelection *sel,
                                       gpointer self_gpointer)
{
  HeaderPane *self(static_cast<HeaderPane *>(self_gpointer));

  if (! self->_selection_changed_idle_tag)
  {
    self->_selection_changed_idle_tag =
      g_idle_add(on_selection_changed_idle, self);
  }
}

gboolean HeaderPane ::on_selection_changed_idle(gpointer self_gpointer)
{
  HeaderPane *self(static_cast<HeaderPane *>(self_gpointer));
  bool const have_article = self->get_first_selected_article() != nullptr;
  static char const *actions_that_need_an_article[] = {
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
    "cancel-article"};

  for (int i = 0, n = G_N_ELEMENTS(actions_that_need_an_article); i < n; ++i)
  {
    self->_action_manager.sensitize_action(actions_that_need_an_article[i],
                                           have_article);
  }

  self->_selection_changed_idle_tag = 0;
  return false;
}

Data::ShowType _show_type;

HeaderPane ::HeaderPane(ActionManager &action_manager,
                        Data &data,
                        Queue &queue,
                        ArticleCache &cache,
                        Prefs &prefs,
                        GroupPrefs &group_prefs,
                        WaitUI &wait,
                        GUI &gui) :
  _action_manager(action_manager),
  _data(data),
  _queue(queue),
  _prefs(prefs),
  _group_prefs(group_prefs),
  _wait(wait),
  _atree(nullptr),
  _root(nullptr),
  _tree_view(nullptr),
  _tree_store(nullptr),
  _selection_changed_idle_tag(0),
  _fg(),
  _bg(),
  _cache(cache),
  _gui(gui),
  _cleared(true)
{
  _fg = prefs.get_color_str("text-color-fg", colors.def_fg.c_str());
  _bg = prefs.get_color_str("text-color-bg", colors.def_bg.c_str());

  // init the icons
  for (guint i = 0; i < ICON_QTY; ++i)
  {
    _icons[i].pixbuf = load_icon(_icons[i].icon_file);
  }

  // initialize the show type...
  const std::string show_type_str(
    prefs.get_string("header-pane-show-matching", "articles"));
  if (show_type_str == "threads")
  {
    _show_type = Data::SHOW_THREADS;
  }
  else if (show_type_str == "subthreads")
  {
    _show_type = Data::SHOW_SUBTHREADS;
  }
  else
  {
    _show_type = Data::SHOW_ARTICLES;
  }

  // build the view...
  GtkWidget *w = _tree_view = gtk_tree_view_new();
  gtk_tree_view_set_enable_search(GTK_TREE_VIEW(w), false);
  gtk_tree_view_set_fixed_height_mode(GTK_TREE_VIEW(w), true);
  gtk_tree_view_set_rubber_banding(GTK_TREE_VIEW(w), true);

  GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(w));
  gtk_tree_selection_set_mode(sel, GTK_SELECTION_MULTIPLE);
  g_signal_connect(sel, "changed", G_CALLBACK(on_selection_changed), this);
  on_selection_changed(sel, this);

  g_signal_connect(
    w, "button-release-event", G_CALLBACK(on_button_pressed), this);
  g_signal_connect(
    w, "button-press-event", G_CALLBACK(on_button_pressed), this);

  /* intercept ENTER and left/right for article selection */
  g_signal_connect(
    w, "key-press-event", G_CALLBACK(on_keyboard_button_pressed), this);

  g_signal_connect(w, "row-collapsed", G_CALLBACK(row_collapsed_cb), nullptr);
  g_signal_connect(w, "row-expanded", G_CALLBACK(row_expanded_cb), nullptr);
  g_signal_connect(w, "popup-menu", G_CALLBACK(on_popup_menu), this);
  g_signal_connect(w, "row-activated", G_CALLBACK(on_row_activated), this);
  GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll), w);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
                                      GTK_SHADOW_IN);

  build_tree_columns();

  // join rows
  _root = scroll;

  search_activate(this); // calls rebuild_filter
  _prefs._rules_enabled = prefs.get_flag("enable-rules", true);
  rules(_prefs._rules_enabled);

  _data.add_listener(this);
  _prefs.add_listener(this);
  _queue.add_listener(this);
  _cache.add_listener(this);

  refresh_font();
}

void HeaderPane ::set_focus()
{
  gtk_widget_grab_focus(_tree_view);
}

void HeaderPane ::select_all()
{
  GtkTreeView *tv(GTK_TREE_VIEW(_tree_view));
  GtkTreeSelection *sel(gtk_tree_view_get_selection(tv));
  gtk_tree_selection_select_all(sel);
}

void HeaderPane ::unselect_all()
{
  GtkTreeView *tv(GTK_TREE_VIEW(_tree_view));
  GtkTreeSelection *sel(gtk_tree_view_get_selection(tv));
  gtk_tree_selection_unselect_all(sel);
}

namespace {
void select_subpaths(GtkTreeView *view,
                     GtkTreeModel *model,
                     GtkTreeSelection *sel,
                     GtkTreePath *begin_path)
{
  gtk_tree_view_expand_row(view, begin_path, true);
  GtkTreeIter begin;
  gtk_tree_model_get_iter(model, &begin, begin_path);
  GtkTreeIter end = begin;
  int n;
  while ((n = gtk_tree_model_iter_n_children(model, &end)))
  {
    GtkTreeIter tmp = end;
    gtk_tree_model_iter_nth_child(model, &end, &tmp, n - 1);
  }
  GtkTreePath *end_path = gtk_tree_model_get_path(model, &end);
  gtk_tree_selection_select_range(sel, begin_path, end_path);
  gtk_tree_path_free(end_path);
}

void select_threads_helper(GtkTreeView *view, bool subthreads_only)
{
  GtkTreeModel *model;
  GtkTreeSelection *sel(gtk_tree_view_get_selection(view));
  GList *list(gtk_tree_selection_get_selected_rows(sel, &model));

  for (GList *l(list); l; l = l->next)
  {
    GtkTreePath *path(static_cast<GtkTreePath *>(l->data));
    if (! subthreads_only)
    {
      while (gtk_tree_path_get_depth(path) > 1) // up to root
      {
        gtk_tree_path_up(path);
      }
    }
    select_subpaths(view, model, sel, path);
  }

  g_list_foreach(list, (GFunc)gtk_tree_path_free, nullptr);
  g_list_free(list);
}
} // namespace

void HeaderPane ::select_threads()
{
  select_threads_helper(GTK_TREE_VIEW(_tree_view), false);
}

void HeaderPane ::select_subthreads()
{
  select_threads_helper(GTK_TREE_VIEW(_tree_view), true);
}

void HeaderPane ::expand_selected()
{
  // get a list of paths
  GtkTreeModel *model(nullptr);
  GtkTreeView *view(GTK_TREE_VIEW(_tree_view));
  GtkTreeSelection *selection(gtk_tree_view_get_selection(view));
  GList *list(gtk_tree_selection_get_selected_rows(selection, &model));
  if (list)
  {
    GtkTreePath *path(static_cast<GtkTreePath *>(list->data));
    gtk_tree_view_expand_row(view, path, true);
    gtk_tree_view_expand_to_path(view, path);
  }
  g_list_foreach(list, (GFunc)gtk_tree_path_free, nullptr);
  g_list_free(list);
}

/***
****
****  NAVIGATION
****
***/

namespace {
/**
***  Navigation Functors
**/

struct TreeIteratorNext : public TreeIterFunctor
{
    virtual ~TreeIteratorNext()
    {
    }

    bool operator()(GtkTreeModel *model, GtkTreeIter *iter) const override
    {
      return model && PAN_TREE_STORE(model)->get_next(iter);
    }

    bool front(GtkTreeModel *model, GtkTreeIter *setme) const override
    {
      return model && PAN_TREE_STORE(model)->front(setme);
    }
};

struct TreeIteratorPrev : public TreeIterFunctor
{
    virtual ~TreeIteratorPrev()
    {
    }

    bool operator()(GtkTreeModel *model, GtkTreeIter *iter) const override
    {
      return model && PAN_TREE_STORE(model)->get_prev(iter);
    }

    bool front(GtkTreeModel *model, GtkTreeIter *setme) const override
    {
      return model && PAN_TREE_STORE(model)->back(setme);
    }
};

/**
***  Article Test Functors
**/

struct ArticleExists : public ArticleTester
{
    virtual ~ArticleExists()
    {
    }

    ArticleExists()
    {
    }

    bool operator()(Article const &) const override
    {
      return true;
    }
};

struct ArticleIsFlagged : public ArticleTester
{
    virtual ~ArticleIsFlagged()
    {
    }

    Article const *article;

    ArticleIsFlagged(Article const *a) :
      article(a)
    {
    }

    bool operator()(Article const &a) const override
    {
      return a.get_flag() && a.message_id != article->message_id;
    }
};

struct ArticleIsParentOf : public ArticleTester
{
    virtual ~ArticleIsParentOf()
    {
    }

    ArticleIsParentOf(Data::ArticleTree const &tree, Article const *a)
    {
      Article const *parent = a ? tree.get_parent(a->message_id) : nullptr;
      _mid = parent ? parent->message_id : "";
    }

    Quark _mid;

    bool operator()(Article const &a) const override
    {
      return _mid == a.message_id;
    }
};

struct ArticleIsUnread : public ArticleTester
{
    virtual ~ArticleIsUnread()
    {
    }

    ArticleIsUnread(Data const &data) :
      _data(data)
    {
    }

    Data const &_data;

    bool operator()(Article const &a) const override
    {
      return ! _data.is_read(&a);
    }
};

struct ArticleIsNotInThread : public ArticleTester
{
    virtual ~ArticleIsNotInThread()
    {
    }

    ArticleIsNotInThread(Data::ArticleTree const &tree, Article const *a) :
      _tree(tree),
      _root(a ? get_root_mid(a) : "")
    {
    }

    Data::ArticleTree const &_tree;
    const Quark _root;

    Quark get_root_mid(Article const *a) const
    {
      for (;;)
      {
        const Quark mid(a->message_id);
        Article const *parent = _tree.get_parent(mid);
        if (! parent)
        {
          return mid;
        }
        a = parent;
      }
    }

    bool operator()(Article const &a) const override
    {
      return _root != get_root_mid(&a);
    }
};

struct ArticleIsUnreadAndNotInThread : public ArticleTester
{
    virtual ~ArticleIsUnreadAndNotInThread()
    {
    }

    ArticleIsUnreadAndNotInThread(Data const &data,
                                  Data::ArticleTree const &tree,
                                  Article const *a) :
      _aiu(data),
      _ainit(tree, a)
    {
    }

    const ArticleIsUnread _aiu;
    const ArticleIsNotInThread _ainit;

    bool operator()(Article const &a) const override
    {
      return _aiu(a) && _ainit(a);
    }
};

/**
***  Action Functors
**/

struct SelectFunctor : public pan::RowActionFunctor
{
    virtual ~SelectFunctor()
    {
    }

    SelectFunctor(GtkTreeView *view) :
      _view(view)
    {
    }

    GtkTreeView *_view;

    void operator()(GtkTreeModel *model,
                    GtkTreeIter *iter,
                    Article const &) override
    {
      GtkTreeSelection *sel(gtk_tree_view_get_selection(_view));
      gtk_tree_selection_unselect_all(sel);
      GtkTreePath *path = gtk_tree_model_get_path(model, iter);
      gtk_tree_view_expand_row(_view, path, true);
      gtk_tree_view_expand_to_path(_view, path);
      gtk_tree_view_set_cursor(_view, path, nullptr, FALSE);
      gtk_tree_view_scroll_to_cell(_view, path, nullptr, true, 0.5f, 0.0f);
      gtk_tree_path_free(path);
    }
};

struct ReadFunctor : public SelectFunctor
{
    virtual ~ReadFunctor()
    {
    }

    ReadFunctor(GtkTreeView *view, ActionManager &am) :
      SelectFunctor(view),
      _am(am)
    {
    }

    ActionManager &_am;

    void operator()(GtkTreeModel *model,
                    GtkTreeIter *iter,
                    Article const &a) override
    {
      SelectFunctor::operator()(model, iter, a);
      maybe_activate_on_idle(
        _view, gtk_tree_model_get_path(model, iter), nullptr);
    }
};
} // namespace

/**
***
**/

void HeaderPane ::find_next_iterator_from(GtkTreeModel *model,
                                          GtkTreeIter *start_pos,
                                          TreeIterFunctor const &iterate_func,
                                          ArticleTester const &test_func,
                                          RowActionFunctor &success_func,
                                          bool test_the_start_pos)
{
  g_assert(start_pos != nullptr);
  GtkTreeIter march = *start_pos;
  bool success(false);
  Article const *article(nullptr);
  for (;;)
  {
    if (test_the_start_pos)
    {
      test_the_start_pos = false;
    }
    else
    {
      if (! iterate_func(model, &march))
      {
        break;
      }
      if ((start_pos->stamp == march.stamp) // PanTreeStore iter equality:
          && (start_pos->user_data == march.user_data)) // have we looped?
      {
        break;
      }
    }
    article = get_article(model, &march);
    if ((success = test_func(*article)))
    {
      break;
    }
  }
  if (success)
  {
    success_func(model, &march, *article);
  }
}

namespace {
bool get_first_selection(GtkTreeSelection *sel, GtkTreeIter *setme)
{
  GtkTreeModel *model(nullptr);
  GList *list(gtk_tree_selection_get_selected_rows(sel, &model));
  bool const found(
    list && gtk_tree_model_get_iter(model, setme, (GtkTreePath *)(list->data)));
  g_list_foreach(list, (GFunc)gtk_tree_path_free, nullptr);
  g_list_free(list);
  return found;
}
} // namespace

void HeaderPane ::next_iterator(GtkTreeView *view,
                                GtkTreeModel *model,
                                TreeIterFunctor const &iterate_func,
                                ArticleTester const &test_func,
                                RowActionFunctor &success_func)
{
  GtkTreeSelection *sel = gtk_tree_view_get_selection(view);
  GtkTreeIter iter;
  if (get_first_selection(sel, &iter))
  {
    find_next_iterator_from(
      model, &iter, iterate_func, test_func, success_func, false);
  }
  else if (iterate_func.front(model, &iter))
  {
    find_next_iterator_from(
      model, &iter, iterate_func, test_func, success_func, true);
  }
}

void HeaderPane ::action_next_if(ArticleTester const &test,
                                 RowActionFunctor &action)
{
  GtkTreeView *v(GTK_TREE_VIEW(_tree_view));
  GtkTreeModel *m(GTK_TREE_MODEL(_tree_store));
  next_iterator(v, m, TreeIteratorNext(), test, action);
}

void HeaderPane ::read_next_if(ArticleTester const &test)
{

  GtkTreeView *v(GTK_TREE_VIEW(_tree_view));
  ReadFunctor read(v, _action_manager);
  action_next_if(test, read);
}

void HeaderPane ::read_prev_if(ArticleTester const &test)
{

  GtkTreeView *v(GTK_TREE_VIEW(_tree_view));
  GtkTreeModel *m(GTK_TREE_MODEL(_tree_store));
  ReadFunctor read(v, _action_manager);
  next_iterator(v, m, TreeIteratorPrev(), test, read);
}

void HeaderPane ::select_next_if(ArticleTester const &test)
{

  GtkTreeView *v(GTK_TREE_VIEW(_tree_view));
  SelectFunctor sel(v);
  action_next_if(test, sel);
}

void HeaderPane ::select_prev_if(ArticleTester const &test)
{

  GtkTreeView *v(GTK_TREE_VIEW(_tree_view));
  GtkTreeModel *m(GTK_TREE_MODEL(_tree_store));
  SelectFunctor sel(v);
  next_iterator(v, m, TreeIteratorPrev(), test, sel);
}

void HeaderPane ::read_next_unread_article()
{
  read_next_if(ArticleIsUnread(_data));
}

void HeaderPane ::read_previous_article() {
  if (_atree && _atree->size() > 0)
    read_prev_if(ArticleExists());
}

void HeaderPane ::read_next_article() {
  if (_atree && _atree->size() > 0)
    read_next_if(ArticleExists());
}

void HeaderPane ::read_next_thread()
{
  if (_atree && _atree->size() > 0)
  {
    read_next_if(ArticleIsNotInThread(*_atree, get_first_selected_article()));
  }
}

void HeaderPane ::read_next_unread_thread()
{
  if (_atree && _atree->size() > 0)
  {
    read_next_if(ArticleIsUnreadAndNotInThread(
      _data, *_atree, get_first_selected_article()));
  }
}

void HeaderPane ::read_previous_thread()
{
  if (_atree && _atree->size() > 0)
  {
    read_prev_if(ArticleIsNotInThread(*_atree, get_first_selected_article()));
  }
}

void HeaderPane ::read_parent_article()
{
  if (_atree && _atree->size() > 0)
  {
    read_prev_if(ArticleIsParentOf(*_atree, get_first_selected_article()));
  }
}

/***
****
***/

void HeaderPane ::move_to_next_bookmark(int step)
{
  bool const forward(step == 1);
  Article *a(get_first_selected_article());
  if (! a)
  {
    return;
  }

  if (forward)
  {
    select_next_if(ArticleIsFlagged(get_first_selected_article()));
  }
  else
  {
    select_prev_if(ArticleIsFlagged(get_first_selected_article()));
  }
}

/***
****
***/

void HeaderPane ::refresh_font()
{
  if (! _prefs.get_flag("header-pane-font-enabled", false))
  {
    gtk_widget_override_font(_tree_view, nullptr);
  }
  else
  {
    const std::string str(_prefs.get_string("header-pane-font", "Sans 10"));
    PangoFontDescription *pfd(pango_font_description_from_string(str.c_str()));
    gtk_widget_override_font(_tree_view, pfd);
    pango_font_description_free(pfd);
  }
}

void HeaderPane ::on_prefs_flag_changed(StringView const &key, bool)
{
  if (key == "header-pane-font-enabled")
  {
    refresh_font();
  }
  if (key == "thread-headers")
  {
    rebuild();
  }
}

void HeaderPane ::on_prefs_string_changed(StringView const &key,
                                          StringView const &)
{
  if (key == "header-pane-font")
  {
    refresh_font();
  }
  else if (key == "header-pane-columns")
  {
    build_tree_columns();
  }
}

void HeaderPane ::on_prefs_color_changed(StringView const &key, GdkRGBA const &)
{
  if (key == "text-color-fg" || key == "text-color-bg")
  {
    _fg = _prefs.get_color_str("text-color-fg", colors.def_fg).c_str();
    _bg = _prefs.get_color_str("text-color-bg", colors.def_bg).c_str();
    refresh_font();
    build_tree_columns();
  }
}

/***
****
***/

void HeaderPane ::rebuild_article_action(Quark const &message_id)
{
  Row *row(get_row(message_id));
  if (row)
  {
    row->action = get_article_action(row->article, _cache, _queue, message_id);
    _tree_store->row_changed(row);
  }
}

void HeaderPane ::on_article_flag_changed(articles_t &a, Quark const &group)
{
  g_return_if_fail(! a.empty());
  foreach_const (articles_t, a, it)
  {
    rebuild_article_action((*it)->message_id);
  }
}

void HeaderPane ::on_queue_tasks_added(Queue &queue, int index, int count)
{
  for (size_t i(index), end(index + count); i != end; ++i)
  {
    TaskArticle const *task(dynamic_cast<TaskArticle const *>(queue[i]));
    if (task)
    {
      rebuild_article_action(task->get_article().message_id);
    }
  }
}

void HeaderPane ::on_queue_task_removed(Queue &, Task &task, int)
{
  TaskArticle const *ta(dynamic_cast<TaskArticle const *>(&task));
  if (ta)
  {
    rebuild_article_action(ta->get_article().message_id);
  }
}

void HeaderPane ::on_cache_added(Quark const &message_id)
{
  quarks_t q;
  q.insert(message_id);
  // TODO fixme!!
  _data.rescore_articles(_group, q);
  rebuild_article_action(message_id);
}

void HeaderPane ::on_cache_removed(quarks_t const &message_ids)
{
  foreach_const (quarks_t, message_ids, it)
  {
    rebuild_article_action(*it);
  }
}

/***
****
***/

struct HeaderPane::SimilarWalk : public PanTreeStore::WalkFunctor
{
  private:
    GtkTreeSelection *selection;
    const Article source;

  public:
    virtual ~SimilarWalk()
    {
    }

    SimilarWalk(GtkTreeSelection *s, Article const &a) :
      selection(s),
      source(a)
    {
    }

  public:
    virtual bool operator()(PanTreeStore *store,
                            PanTreeStore::Row *,
                            GtkTreeIter *iter,
                            GtkTreePath *)
    {
      Article const *article(get_article(GTK_TREE_MODEL(store), iter));
      if (similar(*article))
      {
        gtk_tree_selection_select_iter(selection, iter);
      }
      return true; // keep marching
    }

  private:
    bool similar(Article const &a) const
    {
      // same author, posted within a day and a half of the source, with a
      // similar subject
      static const size_t SECONDS_IN_DAY(60 * 60 * 24);
      return (a.author == source.author)
             && (fabs(difftime(a.time_posted, source.time_posted))
                 < (SECONDS_IN_DAY * 1.5))
             && (subjects_are_similar(a, source));
    }

    static bool subjects_are_similar(Article const &a, Article const &b)
    {
      // make our own copies of the strings so that we can mutilate them
      std::string sa(a.subject.c_str());
      std::string sb(b.subject.c_str());

      // strip out frequent substrings that tend to skew string_likeness too
      // high
      static char const *const frequent_substrings[] = {
        "mp3", "gif", "jpg", "jpeg", "yEnc"};
      for (size_t i = 0; i != G_N_ELEMENTS(frequent_substrings); ++i)
      {
        std::string::size_type pos;
        char const *needle(frequent_substrings[i]);
        while (((pos = sa.find(needle))) != std::string::npos)
        {
          sa.erase(pos, strlen(needle));
        }
        while (((pos = sb.find(needle))) != std::string::npos)
        {
          sb.erase(pos, strlen(needle));
        }
      }

      // strip out non-alpha characters
      foreach (std::string, sa, it)
      {
        if (! isalpha(*it))
        {
          *it = ' ';
        }
      }
      foreach (std::string, sb, it)
      {
        if (! isalpha(*it))
        {
          *it = ' ';
        }
      }

      // decide how picky we want to be.
      // The shorter the string, the more alike they have to be.
      // longer strings typically include long unique filenames.
      int const min_len(std::min(sa.size(), sb.size()));
      bool const is_short_string(min_len <= 20);
      bool const is_long_string(min_len >= 30);
      double min_closeness;
      if (is_short_string)
      {
        min_closeness = 0.6;
      }
      else if (is_long_string)
      {
        min_closeness = 0.5;
      }
      else
      {
        min_closeness = 0.55;
      }

      return string_likeness(sa, sb) >= min_closeness;
    }

    static double string_likeness(std::string const &a_in,
                                  std::string const &b_in)
    {
      double retval;
      StringView a(a_in), b(b_in);

      if (! a.strchr(' ')) // only one word, so count common characters
      {
        int common_chars = 0;

        foreach_const (StringView, a, it)
        {
          char const *pos = b.strchr(*it);
          if (pos)
          {
            ++common_chars;
            b.eat_chars(pos - b.str);
          }
        }

        retval = (double)common_chars / a.len;
      }
      else // more than one word, so count common words
      {
        StringView tok;
        int str1_words(0), common_words(0);
        while (a.pop_token(tok))
        {
          ++str1_words;
          char const *pch = b.strstr(tok);
          if (pch)
          {
            ++common_words;
          }
        }
        retval = (double)common_words / str1_words;
      }
      return retval;
    }
};

void HeaderPane ::select_similar()
{
  GtkTreeSelection *sel(gtk_tree_view_get_selection(GTK_TREE_VIEW(_tree_view)));
  Article const *article(get_first_selected_article());
  if (article)
  {
    SimilarWalk similar(sel, *article);
    _tree_store->walk(similar);
  }
}
