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

#include <config.h>
#include <cassert>
#include <deque>
extern "C" {
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/general/debug.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include <pan/general/quark.h>
#include <pan/general/text-match.h>
#include <pan/general/time-elapsed.h>
#include <pan/data/data.h>
#include "group-pane.h"
#include "pad.h"

using namespace pan;

namespace
{
  enum ModelColumns
  {
    COL_UNREAD,
    COL_QTY
  };

  struct MyRow: public PanTreeStore::Row
  {
    public:
      Quark groupname;
      unsigned long unread;

    public:
      virtual void get_value (int column, GValue* setme) {
        switch (column) {
          case COL_UNREAD: set_value_ulong   (setme, unread); break;
        }
      }
  };

  Quark * sub_title_quark (0);
  Quark * other_title_quark (0);
  bool is_group (const Quark& name) {
    return !name.empty() && name!=*sub_title_quark && name!=*other_title_quark;
  }

  std::string
  get_short_name (const StringView& in)
  {
    static const StringView moderated ("moderated");
    static const StringView d ("d");

    StringView myline, long_token;

    // find the long token -- use the last, unless that's "moderated" or "d"
    myline = in;
    myline.pop_last_token (long_token, '.');
    if (!myline.empty() && (long_token==moderated || long_token==d))
      myline.pop_last_token (long_token, '.');

    // build a new string where each token is shortened except for long_token
    std::string out;
    myline = in;
    StringView tok;
    while (myline.pop_token (tok, '.')) {
      out.insert (out.end(), tok.begin(), (tok==long_token ? tok.end() : tok.begin()+1));
      out += '.';
    }
    if (!out.empty())
      out.erase (out.size()-1);

    return out;
  }

  void
  find_matching_groups (const TextMatch           * match,
                        const std::vector<Quark>  & sorted_groups,
                        std::vector<Quark>        & matching_groups)
  {
    typedef std::vector<Quark> quark_v;
    if (!match)
      matching_groups = sorted_groups;
    else {
      matching_groups.reserve (sorted_groups.size());
      foreach_const (quark_v, sorted_groups, it) {
        const Quark& groupname (*it);
        if (!match || match->test (groupname.c_str()))
          matching_groups.push_back (groupname);
      }
    }
  }
}

Quark
GroupPane :: get_selection () const
{
   Quark groupname;

   GtkTreeIter iter;
   GtkTreeModel * model;
   GtkTreeSelection * selection (gtk_tree_view_get_selection (GTK_TREE_VIEW(_tree_view)));
   if (gtk_tree_selection_get_selected (selection, &model, &iter))
     groupname = dynamic_cast<MyRow*>(_tree_store->get_row (&iter))->groupname;

   return groupname;
}

quarks_t
GroupPane :: get_full_selection () const
{
  quarks_t groups;

  // get a list of paths
  GtkTreeModel * model (0);
  GtkTreeSelection * selection (gtk_tree_view_get_selection (GTK_TREE_VIEW(_tree_view)));
  GList * list (gtk_tree_selection_get_selected_rows (selection, &model));

  for (GList *l(list); l; l=l->next) {
    GtkTreePath * path (static_cast<GtkTreePath*>(l->data));
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter (model, &iter, path))
      groups.insert (dynamic_cast<MyRow*>(_tree_store->get_row (&iter))->groupname);
  }

  // cleanup
  g_list_foreach (list, (GFunc)gtk_tree_path_free, NULL);
  g_list_free (list);

  return groups;
}

void
GroupPane ::  do_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer pane_g)
{
  GroupPane * self (static_cast<GroupPane*>(pane_g));
  GtkWidget * menu (self->_action_manager.get_action_widget ("/group-pane-popup"));
  gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
                  (event ? event->button : 0),
                  (event ? event->time : 0));
}

namespace
{
  gboolean
  on_row_activated_idle (gpointer pane_g)
  {
    static_cast<GroupPane*>(pane_g)->_action_manager.activate_action ("read-selected-group");
    return false;
  }
}

void
GroupPane :: on_row_activated (GtkTreeView          * treeview,
                               GtkTreePath          * path,
                               GtkTreeViewColumn    * col,
                               gpointer               pane_g)
{
  // user may have activated by selecting the row... wait for the GtkTreeSelection to catch up
  g_idle_add (on_row_activated_idle, pane_g);
}

namespace
{
  bool row_collapsed_or_expanded (false);

  void row_collapsed_or_expanded_cb (GtkTreeView *view, GtkTreeIter *iter, GtkTreePath *path, gpointer unused)
  {
    row_collapsed_or_expanded = true;
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

gboolean
GroupPane :: on_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  GroupPane * pane (static_cast<GroupPane*>(userdata));

  // single click with the right mouse button?
  if (event->type == GDK_BUTTON_PRESS && event->button == 3)
  {
    GtkTreeSelection * selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));
    GtkTreePath * path;
    if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW(treeview),
                                       (gint)event->x, (gint)event->y,
                                       &path, NULL, NULL, NULL)) {
      gtk_tree_selection_unselect_all (selection);
      gtk_tree_selection_select_path (selection, path);
      gtk_tree_path_free (path);
    }
    do_popup_menu (treeview, event, userdata);
    return true;
  }
  else if (pane->_prefs.get_flag("single-click-activates-group",true)
           && (event->type == GDK_BUTTON_RELEASE)
           && (event->button == 1)
           && (event->send_event == false)
           && (event->window == gtk_tree_view_get_bin_window (GTK_TREE_VIEW(treeview)))
           && !(event->state & (GDK_SHIFT_MASK|GDK_CONTROL_MASK|GDK_MOD1_MASK)))
  {
    GtkTreePath * path;
    GtkTreeViewColumn * col;
    gint cell_x(0), cell_y(0);
    if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
                                      (gint) event->x,
                                      (gint) event->y,
                                      &path, &col, &cell_x, &cell_y))
      maybe_activate_on_idle (GTK_TREE_VIEW(treeview), path, col);
  }
  else if ((event->type == GDK_BUTTON_RELEASE)
           && (event->button == 2)
           && (event->send_event == false)
           && (event->window == gtk_tree_view_get_bin_window (GTK_TREE_VIEW(treeview))))
  {
    pane->_action_manager.activate_action ("clear-header-pane");
    pane->_action_manager.activate_action ("clear-body-pane");
  }

  return false;
}


gboolean
GroupPane :: on_popup_menu (GtkWidget *treeview, gpointer userdata)
{
  do_popup_menu (treeview, NULL, userdata);
  return true;
}

namespace
{
  typedef std::vector<GtkTreeIter> tree_iters_t;

  struct MyRowLessThan {
    bool operator() (const MyRow* a, const MyRow* b) const {
      return a->groupname < b->groupname;
    }
  };

  typedef sorted_vector<MyRow*, true, MyRowLessThan> rows_t;

  rows_t _group_rows;

  MyRow* find_row (const Quark& groupname)
  {
    MyRow tmp;
    tmp.groupname = groupname;
    rows_t::iterator it = _group_rows.find (&tmp);
    return it==_group_rows.end() ? 0 : *it;
  }

  void
  delete_rows (gpointer data, GObject * dead_object)
  {
    delete [] static_cast<MyRow*>(data);
  }

  // give PanTreeStore a noop row dispose functor
  // since we allocate our rows in a block.
  struct NoopRowDispose: public PanTreeStore::RowDispose
  {
    virtual ~NoopRowDispose () { }
    virtual void row_dispose (PanTreeStore *store, PanTreeStore::Row* row) { }
  };

  NoopRowDispose noop_row_dispose;

  PanTreeStore*
  build_model (const Data         & data,
               const TextMatch    * match,
               tree_iters_t       & expandme,
               rows_t             & setme_rows)
  {
    // build the model...
    PanTreeStore * store = PanTreeStore :: new_tree (COL_QTY, G_TYPE_ULONG);  // unread
    store->set_row_dispose (&noop_row_dispose);

    // find the groups that we'll be adding.
    std::vector<Quark> groups, sub, unsub;
    data.get_other_groups (groups);
    find_matching_groups (match, groups, unsub);
    groups.clear ();
    data.get_subscribed_groups (groups);
    find_matching_groups (match, groups, sub);

    std::vector<MyRow*>& group_rows (setme_rows.get_container());
    group_rows.clear ();
    group_rows.reserve (sub.size() + unsub.size());

    MyRow * headers = new MyRow[2];
    headers[0].groupname = *sub_title_quark;
    headers[1].groupname = *other_title_quark;
    g_object_weak_ref (G_OBJECT(store), delete_rows, headers);
 
    // 
    //  subscribed
    //

    MyRow * row = &headers[0];
    store->append (NULL, row);
    if (!sub.empty())
    {
      const size_t n (sub.size());
      std::vector<PanTreeStore::Row*> appendme;
      appendme.reserve (n);
      MyRow *rows(new MyRow [n]), *r(rows);
      g_object_weak_ref (G_OBJECT(store), delete_rows, rows);

      unsigned long unused;
      for (size_t i(0); i!=n; ++i, ++r) {
        r->groupname = sub[i];
        data.get_group_counts (r->groupname, r->unread, unused);
        appendme.push_back (r);
        group_rows.push_back (r);
      }

      store->append (row, appendme);
      expandme.push_back (store->get_iter (row));
    }

    //
    // unsubscribed
    //

    row = &headers[1];
    store->append (NULL, row);
    if (!unsub.empty())
    {
      const size_t n (unsub.size());
      std::vector<PanTreeStore::Row*> appendme;
      appendme.reserve (n);
      MyRow *rows(new MyRow[n]), *r(rows);
      g_object_weak_ref (G_OBJECT(store), delete_rows, rows);

      unsigned long unused;
      for (size_t i(0); i!=n; ++i, ++r) {
        r->groupname = unsub[i];
        data.get_group_counts (r->groupname, r->unread, unused);
        appendme.push_back (r);
        group_rows.push_back (r);
      }

      store->append (row, appendme);

      // only expand the unsub expander if user specified search criteria,
      // or if there's no criteria and no subscribed groups (first-time usrs).
      // otherwise it's a flood of thousands of groups.
      // they can click the expander themselves to get that,
      const bool user_did_search (match != 0);
      if (!unsub.empty() && (user_did_search || sub.empty()))
        expandme.push_back (store->get_iter (row));
    }

    setme_rows.sort ();
    return store;
  }
}

/***
****  Data listener
***/

void
GroupPane :: on_group_subscribe (const Quark& groupname, bool sub)
{
  GtkTreeModel * model (GTK_TREE_MODEL (_tree_store));

  // find out where it should be moved to
  int pos (0);
  GtkTreeIter section_iter, group_iter;
  if (gtk_tree_model_iter_nth_child (model, &section_iter, NULL, (sub?0:1))) {
    if (gtk_tree_model_iter_children (model, &group_iter, &section_iter)) do {
      MyRow * row (dynamic_cast<MyRow*>(_tree_store->get_row (&group_iter)));
      if (groupname.to_string() < row->groupname.c_str())
        break;
      ++pos;
    } while (gtk_tree_model_iter_next (model, &group_iter));
  }
  
  // move the row
  _tree_store->reparent (_tree_store->get_row(&section_iter), find_row(groupname), pos);

  // make sure it's visible...
  if (sub) {
    GtkTreePath * path = gtk_tree_model_get_path (model, &section_iter);
    gtk_tree_view_expand_row (GTK_TREE_VIEW(_tree_view), path, false);
    gtk_tree_path_free (path);
  }
}

void
GroupPane :: refresh_dirty_groups ()
{
  foreach_const (quarks_t, _dirty_groups, it)
  {
    MyRow * row (find_row (*it));
    if (row)
    {
      unsigned long unused;
      _data.get_group_counts (row->groupname, row->unread, unused);
      _tree_store->row_changed (row);
    }
  }

  _dirty_groups_idle_tag = 0;
  _dirty_groups.clear ();
}
gboolean
GroupPane :: dirty_groups_idle (gpointer user_data)
{
  static_cast<GroupPane*>(user_data)->refresh_dirty_groups ();
  return false;
}

void
GroupPane :: on_group_read (const Quark& groupname)
{
  _dirty_groups.insert (groupname);

  if (!_dirty_groups_idle_tag)
       _dirty_groups_idle_tag = g_timeout_add (333, dirty_groups_idle, this);
}
void
GroupPane :: on_group_counts (const Quark& groupname, unsigned long unread, unsigned long total)
{
  on_group_read (groupname);
}

/***
****  Newsgroup Filtering
***/

namespace
{
  void
  expand_iterators (const tree_iters_t& iters, GtkTreeModel * model, GtkTreeView * view)
  {
    foreach_const (tree_iters_t, iters, it)
    {
      GtkTreePath * path = gtk_tree_model_get_path (model, const_cast<GtkTreeIter*>(&*it));
      gtk_tree_view_expand_row (view, path, true);
      gtk_tree_path_free (path);
    }
  }
}

void
GroupPane :: set_filter (const std::string& search_text)
{
   GtkTreeView * view = GTK_TREE_VIEW(_tree_view);

   // get the current selection
   typedef std::set<GtkTreeRowReference*> sel_refs_t;
   sel_refs_t sel_refs;
   GtkTreeSelection * selection = gtk_tree_view_get_selection(view);
   GList * tmp = gtk_tree_selection_get_selected_rows (selection, NULL);
   GtkTreeModel * old_model = gtk_tree_view_get_model (view);
   for (GList * l=tmp; l!=0; l=l->next) {
      GtkTreePath * path = static_cast<GtkTreePath*>(l->data);
      sel_refs.insert (gtk_tree_row_reference_new (old_model, path));
      gtk_tree_path_free (path);
   }
   g_list_free (tmp);

   // pmatch will point to a local TextMatch matching on the filter-phrase,
   // or be a NULL pointer if the filter-phrase is empty
   TextMatch match;
   TextMatch * pmatch (0);
   if (!search_text.empty()) {
     match.set (search_text, TextMatch::CONTAINS, false);
     pmatch = &match;
   }

   // build and use the new store
   tree_iters_t iters;
   PanTreeStore * store = build_model (_data, pmatch, iters, _group_rows);
   _tree_store = store;
   gtk_tree_view_set_model(view, GTK_TREE_MODEL(store));
   g_object_unref (G_OBJECT(store));

   // expand whatever paths need to be expanded
   expand_iterators (iters, GTK_TREE_MODEL(store), GTK_TREE_VIEW(_tree_view));

   // restore the selection
   for (sel_refs_t::iterator it=sel_refs.begin(), end=sel_refs.end(); it!=end; ++it) {
      GtkTreePath * path = gtk_tree_row_reference_get_path (*it);
      gtk_tree_selection_select_path  (selection, path);
      gtk_tree_path_free (path);
      gtk_tree_row_reference_free (*it);
   }
   sel_refs.clear ();
}

namespace
{
  guint entry_changed_tag (0u);
  guint activate_soon_tag (0u);

  std::string search_text;

 void set_search_entry (GtkWidget * entry, const char * s)
  {
    g_signal_handler_block (entry, entry_changed_tag);
    gtk_entry_set_text (GTK_ENTRY(entry), s);
    g_signal_handler_unblock (entry, entry_changed_tag);
  }

  gboolean search_entry_focus_in_cb (GtkWidget     * w,
                                     GdkEventFocus * unused1,
                                     gpointer        unused2)
  {
    gtk_widget_modify_text (w, GTK_STATE_NORMAL, NULL); // resets
    set_search_entry (w, search_text.c_str());
    return false;
  }

  void refresh_search_entry (GtkWidget * w)
  {
    if (search_text.empty() && !GTK_WIDGET_HAS_FOCUS(w))
    {
      GdkColor c;
      c.pixel = 0;
      c.red = c.green = c.blue = 0xAAAA; // light grey
      gtk_widget_modify_text (w, GTK_STATE_NORMAL, &c);
      set_search_entry (w, _("Group Name"));
    }
  }

  gboolean search_entry_focus_out_cb (GtkWidget     * w,
                                      GdkEventFocus * unused1,
                                      gpointer        unused2)
  {
    refresh_search_entry (w);
    return false;
  }

  void search_activate (GroupPane * pane)
  {
    pane->set_filter (search_text);
  }

  void remove_activate_soon_tag ()
  {
    if (activate_soon_tag != 0)
    {
      g_source_remove (activate_soon_tag);
      activate_soon_tag = 0;
    }
  }

  void search_entry_activated (GtkEntry * entry, gpointer pane_gpointer)
  {
    search_activate (static_cast<GroupPane*>(pane_gpointer));
    remove_activate_soon_tag ();
  }

  void reset_entry_filter_cb (GtkWidget * button, gpointer pane_gpointer)
  {
    GtkWidget * e = GTK_WIDGET (g_object_get_data (G_OBJECT(button), "entry"));
    set_search_entry (e, "");
    search_text.clear ();
    search_entry_activated (NULL, pane_gpointer);
  }

  gboolean activated_timeout_cb (gpointer pane_gpointer)
  {
    search_activate (static_cast<GroupPane*>(pane_gpointer));
    remove_activate_soon_tag ();
    return false; // remove the source
  }

  void bump_activate_soon_tag (GroupPane * pane)
  {
    remove_activate_soon_tag ();
    activate_soon_tag = g_timeout_add (1000, activated_timeout_cb, pane);
  }

  void search_entry_changed_by_user (GtkEditable * e, gpointer pane_gpointer)
  {
    search_text = gtk_entry_get_text (GTK_ENTRY(e));
    bump_activate_soon_tag (static_cast<GroupPane*>(pane_gpointer));

    GtkWidget * b (GTK_WIDGET (g_object_get_data (G_OBJECT(e), "reset-button")));
    gtk_widget_set_sensitive (b, !search_text.empty());
  }
}

void
GroupPane :: on_grouplist_rebuilt ()
{
  search_activate (this);
}


namespace
{
  bool shorten;

  void
  render_group_name (GtkTreeViewColumn * col,
                     GtkCellRenderer   * renderer,
                     GtkTreeModel      * model,
                     GtkTreeIter       * iter,
                     gpointer            user_data)
  {
    PanTreeStore * tree (PAN_TREE_STORE(model));
    MyRow * row (dynamic_cast<MyRow*>(tree->get_row (iter)));
    const unsigned long& unread (row->unread);
    //const unsigned long& total (row->total);
    const Quark& name (row->groupname);

    const bool is_g (is_group(name));
    const bool do_shorten (shorten && is_g);
    std::string group_name (do_shorten ? get_short_name(StringView(name)) : name.to_string());
    if (is_g && unread) {
      char buf[64];
      g_snprintf (buf, sizeof(buf), " (%lu)", unread);
      group_name += buf;
    }
    //if (unread || total) {
      //if (unread)
      //  g_snprintf (buf, sizeof(buf), _(" (%lu of %lu)"), unread, total);
      //else
    g_object_set (renderer, "text", group_name.c_str(),
                            "weight", (!is_g || unread ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL),
                            NULL);
  }
}

void
GroupPane :: set_name_collapse (bool b)
{
  shorten = b;
  gtk_widget_queue_draw (_tree_view);
}

/***
****
***/

namespace
{
  struct NextGroupWalk: public PanTreeStore::WalkFunctor
  {
    const ActionManager& am;
    GtkTreeView * view;
    GtkTreeSelection * sel;
    const Quark current_group;
    bool current_group_reached;
    const bool unread_only;

    NextGroupWalk (const ActionManager& a, GtkTreeView *v, const Quark& g, bool unread):
      am (a),
      view (v),
      sel (gtk_tree_view_get_selection (view)),
      current_group(g),  
      current_group_reached (g.empty()),
      unread_only (unread) { }
    virtual ~NextGroupWalk () { }

    virtual bool operator()(PanTreeStore * store, PanTreeStore::Row * r,
                            GtkTreeIter * iter, GtkTreePath * unused)
    {
      const MyRow * row (dynamic_cast<MyRow*>(r));
      const Quark& name (row->groupname);
      if (is_group (name)) {
        if (current_group_reached && (row->unread || !unread_only)) {
          GtkTreePath * path = gtk_tree_model_get_path (GTK_TREE_MODEL(store), iter);
          gtk_tree_view_expand_row (view, path, true);
          gtk_tree_view_expand_to_path (view, path);
          gtk_tree_view_set_cursor (view, path, NULL, false);
          gtk_tree_view_scroll_to_cell (view, path, NULL, true, 0.5f, 0.0f);
          gtk_tree_selection_unselect_all (sel);
          gtk_tree_selection_select_path (sel, path);
          gtk_tree_path_free (path);
          am.activate_action ("read-selected-group");
          return false; // done
        }
        if (name == current_group)
          current_group_reached = true;
      }
      return true; // keep marching
    }
  };
}

void
GroupPane :: read_next_unread_group ()
{
  GtkTreeView * view (GTK_TREE_VIEW (_tree_view));
  const Quark current_group (get_selection ());
  NextGroupWalk walker (_action_manager, view, current_group, true);
  _tree_store->walk (walker);
}

void
GroupPane :: read_next_group ()
{
  GtkTreeView * view (GTK_TREE_VIEW (_tree_view));
  const Quark current_group (get_selection ());
  NextGroupWalk walker (_action_manager, view, current_group, false);
  _tree_store->walk (walker);
}

/***
****
***/

namespace
{
  gboolean
  select_func (GtkTreeSelection * selection,
               GtkTreeModel     * model,
               GtkTreePath      * path,
               gboolean           path_currently_selected,
               gpointer           unused)
  {
    GtkTreeIter iter;
    PanTreeStore * store (PAN_TREE_STORE(model));
    const bool row_can_be_selected =
      gtk_tree_model_get_iter (model, &iter, path)
      && is_group (dynamic_cast<MyRow*>(store->get_row (&iter))->groupname);
    return row_can_be_selected;
  }
}

GtkWidget*
GroupPane :: create_filter_entry ()
{
  GtkWidget * h = gtk_hbox_new (false, 0);
  GtkWidget * entry = gtk_entry_new ();
  gtk_widget_set_size_request (entry, 100, -1);
  _action_manager.disable_accelerators_when_focused (entry);
  g_signal_connect (entry, "focus-in-event", G_CALLBACK(search_entry_focus_in_cb), NULL);
  g_signal_connect (entry, "focus-out-event", G_CALLBACK(search_entry_focus_out_cb), NULL);
  g_signal_connect (entry, "activate", G_CALLBACK(search_entry_activated), this);
  entry_changed_tag = g_signal_connect (entry, "changed", G_CALLBACK(search_entry_changed_by_user), this);
  refresh_search_entry (entry);
  gtk_box_pack_start (GTK_BOX(h), entry, 0, 0, false);

  GtkWidget * w = gtk_button_new ();
  GtkTooltips * tips = gtk_tooltips_new ();
  g_object_ref_sink_pan (G_OBJECT(tips));
  g_object_weak_ref (G_OBJECT(w), (GWeakNotify)g_object_unref, tips);
  g_object_set_data (G_OBJECT(w), "entry", entry);
  g_object_set_data (G_OBJECT(entry), "reset-button", w);
  gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);
  g_signal_connect (w, "clicked", G_CALLBACK(reset_entry_filter_cb), this);
  GtkWidget * image = gtk_image_new_from_stock (GTK_STOCK_CANCEL, GTK_ICON_SIZE_BUTTON);
  gtk_container_add (GTK_CONTAINER(w), image);
  gtk_tooltips_set_tip (GTK_TOOLTIPS(tips), w, _("Clear the Filter"), NULL);
  gtk_box_pack_start (GTK_BOX(h), w, 0, 0, false);
  gtk_widget_set_sensitive (w, false);

  return h;
}

void
GroupPane :: on_selection_changed (GtkTreeSelection * sel, gpointer pane_gpointer)
{
  GroupPane * self (static_cast<GroupPane*>(pane_gpointer));
  Quark group (self->get_selection());
  self->_action_manager.sensitize_action ("show-group-preferences-dialog", is_group(group));
}

GroupPane :: GroupPane (ActionManager& action_manager, Data& data, Prefs& prefs):
  _prefs (prefs),
  _data (data),
  _tree_view (0),
  _action_manager (action_manager),
  _dirty_groups_idle_tag (0)
{
  shorten = prefs.get_flag ("shorten-group-names", false);
  sub_title_quark = new Quark (_("Subscribed Groups"));
  other_title_quark = new Quark (_("Other Groups"));

  quarks_t groups;

  // build this first because _tree_view is needed for a callback...
  tree_iters_t iters;
  _tree_store = build_model (_data, NULL, iters, _group_rows);
  _tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(_tree_store));
  g_object_unref (G_OBJECT(_tree_store)); // will die with the view
  gtk_tree_view_set_enable_search (GTK_TREE_VIEW(_tree_view), false);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(_tree_view), false);
  expand_iterators (iters, GTK_TREE_MODEL(_tree_store), GTK_TREE_VIEW(_tree_view));

  GtkTreeSelection * selection (gtk_tree_view_get_selection (GTK_TREE_VIEW(_tree_view)));
  gtk_tree_selection_set_select_function (selection, select_func, 0, 0);
  g_signal_connect (selection, "changed", G_CALLBACK(on_selection_changed), this);
  on_selection_changed (selection, this);

#if GTK_CHECK_VERSION(2,8,0)
  gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW(_tree_view), true);
#endif

  g_signal_connect (_tree_view, "row_collapsed", G_CALLBACK(row_collapsed_or_expanded_cb), 0);
  g_signal_connect (_tree_view, "row_expanded", G_CALLBACK(row_collapsed_or_expanded_cb), 0);
  g_signal_connect (_tree_view, "button-press-event", G_CALLBACK(on_button_pressed), this);
  g_signal_connect (_tree_view, "button-release-event", G_CALLBACK(on_button_pressed), this);
  g_signal_connect (_tree_view, "popup-menu", G_CALLBACK(on_popup_menu), this);
  g_signal_connect (_tree_view, "row-activated", G_CALLBACK(on_row_activated), this);
  GtkWidget * scroll = gtk_scrolled_window_new (NULL, NULL);

  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);

  gtk_container_add (GTK_CONTAINER(scroll), _tree_view);

   const int xpad (prefs.get_int ("tree-view-row-margins", 1));
   GtkCellRenderer * text_renderer = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT, "ypad", 0, "xpad", xpad, NULL));

   GtkTreeViewColumn * col = gtk_tree_view_column_new ();
   gtk_tree_view_column_set_sizing (col ,GTK_TREE_VIEW_COLUMN_FIXED);
   gtk_tree_view_column_set_fixed_width (col, 200);
   gtk_tree_view_column_set_resizable (col, true);
   gtk_tree_view_column_set_title (col, _("Name"));
   gtk_tree_view_append_column (GTK_TREE_VIEW(_tree_view), col);
   gtk_tree_view_set_expander_column (GTK_TREE_VIEW(_tree_view), col);
   gtk_tree_view_column_pack_start (col, text_renderer, true);
   gtk_tree_view_column_set_cell_data_func (col, text_renderer, render_group_name, 0, 0);

  _root = scroll;
  _data.add_listener (this);
  _prefs.add_listener (this);

  refresh_font ();
}

GroupPane :: ~GroupPane ()
{
  delete sub_title_quark;
  delete other_title_quark;

  _prefs.remove_listener (this);
  _data.remove_listener (this);
}

/***
****
***/

void
GroupPane :: refresh_font ()
{
  if (!_prefs.get_flag ("group-pane-font-enabled", false))
    gtk_widget_modify_font (_tree_view, 0);
  else {
    const std::string str (_prefs.get_string ("group-pane-font", "Sans 10"));
    PangoFontDescription * pfd (pango_font_description_from_string (str.c_str()));
    gtk_widget_modify_font (_tree_view, pfd);
    pango_font_description_free (pfd);
  }
}

void
GroupPane :: on_prefs_flag_changed (const StringView& key, bool value)
{
  if (key == "group-pane-font-enabled")
    refresh_font ();
}

void
GroupPane :: on_prefs_string_changed (const StringView& key, const StringView& value)
{
  if (key == "group-pane-font")
    refresh_font ();
}
