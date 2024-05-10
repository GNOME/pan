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

#ifndef _GroupPane_h_
#define _GroupPane_h_

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/quark.h>
#include <pan/data/data.h>
#include <pan/gui/action-manager.h>
#include <pan/gui/pan-tree.h>
#include <pan/gui/prefs.h>
#include <pan/gui/group-prefs.h>

namespace pan
{
  /**
   * Group Pane in the main window of Pan's GUI.
   * @ingroup GUI
   */
  class GroupPane: private Data::Listener, private Prefs::Listener
  {
    protected: // Data::Listener
      virtual void on_grouplist_rebuilt () override;
      virtual void on_group_read (const Quark& group) override;
      virtual void on_group_subscribe (const Quark& group, bool sub) override;
      virtual void on_group_counts (const Quark& group,
                                    Article_Count unread,
                                    Article_Count total) override;

    public:
      GroupPane (ActionManager&, Data&, Prefs&, GroupPrefs&);
      ~GroupPane ();
      GtkWidget* root () { return _root; }
      GtkWidget* get_default_focus_widget() { return _tree_view; }
      GtkWidget* create_filter_entry ();
      void set_name_collapse (bool);
      Quark get_first_selection () const;
      quarks_v get_full_selection () const;
      void read_next_unread_group ();
      void read_next_group ();

    private:
      Prefs& _prefs;
      GroupPrefs& _group_prefs;
      Data& _data;
      bool _collapsed;
      GtkWidget * _root;
      GtkWidget * _tree_view;
      PanTreeStore * _tree_store;

    public:
      GroupPrefs& get_group_prefs() { return _group_prefs; }
      Prefs& get_prefs() { return _prefs; }

    private:
      GtkTreePath* find_next_subscribed_group (bool unread_only);
      void read_group (GtkTreePath*);
      void read_next_group_impl (bool unread_only);

    public:
      void read_group (const StringView&);
      static bool is_virtual_group (const Quark& group);

    private:
      static void do_popup_menu (GtkWidget*, GdkEventButton*, gpointer);
      static gboolean on_button_pressed (GtkWidget*, GdkEventButton*, gpointer);
      static gboolean on_popup_menu (GtkWidget*, gpointer);
      static void on_row_activated (GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer);
      static void on_selection_changed (GtkTreeSelection*, gpointer);
      void refresh_font ();

    public: // pretend these are private
      void set_filter (const std::string& text, int mode);
      const ActionManager& _action_manager;

    private:
      void on_prefs_flag_changed (const StringView& key, bool value) override;
      void on_prefs_int_changed (const StringView&, int) override { }
      void on_prefs_string_changed (const StringView& key, const StringView& value) override;
      void on_prefs_color_changed (const StringView&, const GdkRGBA&) override;

    private:
      quarks_t _dirty_groups;
      guint _dirty_groups_idle_tag;
      static gboolean dirty_groups_idle (gpointer);
      void refresh_dirty_groups ();
  };
}

#endif
