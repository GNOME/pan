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

#ifndef _HeaderPane_h_
#define _HeaderPane_h_

#include <gtk/gtk.h>
#include <pan/general/e-util.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <pan/data/filter-info.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/tasks/queue.h>
#include <pan/gui/action-manager.h>
#include <pan/gui/pan-tree.h>
#include <pan/gui/prefs.h>

namespace pan
{
  /**
   * Base class for functors taking a const Article& and returning bool.
   * @ingroup GUI
   */
  struct ArticleTester {
    virtual ~ArticleTester() {}
    virtual bool operator()(const Article&) const = 0;
  };

  /**
   * Base class for tree navigation functors
   * @ingroup GUI
   */
  struct TreeIterFunctor {
    virtual ~TreeIterFunctor () {}
    virtual bool operator() (GtkTreeModel*, GtkTreeIter*) const = 0;
    virtual bool front (GtkTreeModel*, GtkTreeIter*) const = 0;
  };

  /**
   * Base class for actions performed on a header pane row.
   */
  struct RowActionFunctor {
    virtual ~RowActionFunctor () {}
    virtual void operator() (GtkTreeModel* model, GtkTreeIter* iter, const Article& a) = 0;
  };

  /**
   * Header Pane in the main window of Pan's GUI.
   * @ingroup GUI
   */
  class HeaderPane:
    private Data::Listener,
    private Data::ArticleTree::Listener,
    private Prefs::Listener,
    private Queue::Listener,
    private ArticleCache::Listener
  {
    public:
      HeaderPane (ActionManager&, Data& data, Queue&, ArticleCache&, Prefs&);
      ~HeaderPane ();

    public:
      void refilter ();
      void set_show_type (const Data::ShowType);

    public:
      void set_focus ();
      void unselect_all ();
      void select_all ();
      void select_threads ();
      void select_subthreads ();
      void select_similar ();
      void expand_selected ();

    public:
      void read_next_article ();
      void read_next_unread_article ();
      void read_next_thread ();
      void read_next_unread_thread ();
      void read_previous_article ();
      void read_previous_thread ();
      void read_parent_article ();

    private:
      void action_next_if (const ArticleTester& test, RowActionFunctor& action);
      void read_next_if (const ArticleTester&);
      void read_prev_if (const ArticleTester&);

    public:
      GtkWidget* root () { return _root; }
      GtkWidget* get_default_focus_widget() { return _tree_view; }
      GtkWidget* create_filter_entry ();
      const Article* get_first_selected_article () const;
      std::set<const Article*> get_full_selection () const;
      std::vector<const Article*> get_full_selection_v () const;
      std::set<const Article*> get_nested_selection () const;
      bool set_group (const Quark& group);
      const Quark& get_group () { return _group; }

    private:
      void rebuild ();
      void rebuild_article_action (const Quark& message_id);
      void rebuild_article_state  (const Quark& message_id);
      void rebuild_all_article_states ();

    private:
      virtual void on_group_read (const Quark& group);

    private:
      virtual void on_tree_change (const Data::ArticleTree::Diffs&);

    private:
      virtual void on_prefs_flag_changed   (const StringView&, bool);
      virtual void on_prefs_int_changed    (const StringView&, int) { }
      virtual void on_prefs_string_changed (const StringView&, const StringView&);
      virtual void on_prefs_color_changed  (const StringView&, const GdkColor&) {}

    private:
      virtual void on_queue_task_active_changed (Queue&, Task&, bool active) { }
      virtual void on_queue_tasks_added (Queue&, int index, int count);
      virtual void on_queue_task_removed (Queue&, Task&, int index);
      virtual void on_queue_task_moved (Queue&, Task&, int new_index, int old_index) { }
      virtual void on_queue_connection_count_changed (Queue&, int count) { }
      virtual void on_queue_size_changed (Queue&, int active, int total) { }
      virtual void on_queue_online_changed (Queue&, bool online) { }
      virtual void on_queue_error (Queue&, const StringView& message) { }

    private:
      virtual void on_cache_added (const Quark& mid);
      virtual void on_cache_removed (const quarks_t& mids);

    public: // pretend it's private
      ActionManager& _action_manager;

    private:

      enum {
        COL_DATE_STR, COL_STATE, COL_ACTION, COL_SCORE,
        COL_LINES, COL_BYTES, COL_DATE, COL_ARTICLE_POINTER,
        COL_COLLATED_SUBJECT, COL_COLLATED_AUTHOR, COL_SUBJECT,
        COL_SHORT_AUTHOR, N_COLUMNS
      };

      class Row: public PanTreeStore::Row
      {
        public:
          const Article * article;
          char * date_str;
          Quark short_author;
          int action;
          int state;
          bool is_read;

        private:
          static Quark build_short_author (const Article * article) {
            return Quark (GNKSA :: get_short_author_name (article->author.c_str()));
          }

        private:
          mutable char * collated_subject;
          mutable char * collated_author;

          // casefolded utf-8-friendly comparison key...
          static char* do_collate (const StringView& view) {
            StringView in (view);
            while (!in.empty()) {
              const gunichar ch = g_utf8_get_char (in.str);
              if (!g_unichar_isalnum (ch)) // eat everything before the first alpha
                in.eat_chars (g_unichar_to_utf8 (ch, NULL));
              else if (in.len>=3 // Re: reply leader
                   && (in.str[0]=='R' ||in.str[0]=='r')
                   && (in.str[1]=='E' || in.str[1]=='e')
                   && in.str[2]==':')
                in.eat_chars (3);
              else
                break;
            }

            if (in.empty())
              return g_strdup ("");

            char * casefold = g_utf8_casefold (in.str, in.len);
            char * ret = g_utf8_collate_key (casefold, -1);
            g_free (casefold);
            return ret;
          }

        public:

          // lazy instantation... it's expensive and user might never sort by this key
          char* get_collated_subject () const {
            if (!collated_subject)
                 collated_subject = do_collate (article->subject.to_view());
            return collated_subject;
          }

          // lazy instantation... it's expensive and user might never sort by this key
          char* get_collated_author () const {
            if (!collated_author)
                 collated_author = do_collate (short_author.to_view());
            return collated_author;
          }

        public:
          Row (const Data& data, const Article * a, char * date_str_in, int action_in, int state_in):
            article(a),
            date_str (date_str_in),
            short_author(build_short_author(a)),
            action (action_in),
            state (state_in),
            is_read (data.is_read (a)),
            collated_subject(0),
            collated_author(0) {}

          virtual ~Row () {
            g_free (collated_subject);
            g_free (collated_author);
            g_free (date_str);
          }

        public:
          virtual void get_value (int column, GValue* setme) {
            switch (column) {
              case COL_DATE_STR:         set_value_static_string (setme, date_str); break;
              case COL_STATE:            set_value_int (setme, state); break;
              case COL_ACTION:           set_value_int (setme, action); break;
              case COL_SCORE:            set_value_int (setme, article->score); break;
              case COL_LINES:            set_value_ulong (setme, article->get_line_count()); break;
              case COL_BYTES:            set_value_ulong (setme, article->get_byte_count()); break;
              case COL_DATE:             set_value_ulong (setme, (unsigned long)article->time_posted); break;
              case COL_ARTICLE_POINTER:  set_value_pointer (setme, (void*)article); break;
              case COL_COLLATED_SUBJECT: set_value_static_string (setme, get_collated_subject()); break;
              case COL_COLLATED_AUTHOR:  set_value_static_string (setme, get_collated_author()); break;
              case COL_SUBJECT:          set_value_static_string (setme, article->author.c_str()); break;
              case COL_SHORT_AUTHOR:     set_value_static_string (setme, short_author.c_str()); break;
            }
          }
      };

      struct RowLessThan
      {
        bool operator () (const Row* a, const Row* b) const {
          return a->article->message_id < b->article->message_id;
        }
        bool operator () (const Row* a, const Quark& b) const {
          return a->article->message_id < b;
        }
        bool operator () (const Quark& a, const Row* b) const {
          return a < b->article->message_id;
        }
      };

      typedef sorted_vector <Row*, true, RowLessThan> mid_to_row_t;

      mid_to_row_t _mid_to_row;

    private:

      static const Article* get_article (GtkTreeModel*, GtkTreeIter*);
      int find_highest_followup_score (GtkTreeModel*, GtkTreeIter*) const;
      Row* create_row (const EvolutionDateMaker&, const Article*);
      void add_children_to_model (PanTreeStore              * store,
                                  PanTreeStore::Row         * parent_row,
                                  const Quark               & parent_mid,
                                  const Data::ArticleTree   * atree,
                                  const EvolutionDateMaker  & date_maker,
                                  const bool                  do_thread);
      PanTreeStore* build_model (const Quark&, Data::ArticleTree*, const TextMatch*);
      void build_tree_columns ();

    private:
      Data& _data;
      Queue& _queue;
      Prefs& _prefs;
      Quark _group;
      Data::ArticleTree * _atree;
      GtkWidget * _root;
      GtkWidget * _tree_view;
      PanTreeStore * _tree_store;
      FilterInfo _filter;
      Data::ShowType _show_type;

    private:
      void rebuild_filter (const std::string&, int);
      void refresh_font ();

    public: // public so that anonymous namespace can reach -- don't call
      void filter (const std::string& text, int mode);
      static void do_popup_menu (GtkWidget*, GdkEventButton*, gpointer);
      static void on_row_activated (GtkTreeView*, GtkTreePath*, GtkTreeViewColumn*, gpointer);
      static gboolean on_button_pressed (GtkWidget*, GdkEventButton*, gpointer);
      ArticleCache& _cache;

    private:
      void get_nested_foreach (GtkTreeModel*, GtkTreePath*, GtkTreeIter*, gpointer) const;
      static void get_nested_foreach_static (GtkTreeModel*, GtkTreePath*, GtkTreeIter*, gpointer);

    private:
      static int column_compare_func (GtkTreeModel*, GtkTreeIter*, GtkTreeIter*, gpointer);
    private:
      class CountUnread;
      class SelectFirstArticle;
      class RowInserter;
      class SimilarWalk;
      void walk_and_collect (GtkTreeModel*, GtkTreeIter*, std::set<const Article*>&) const;
    private:
      typedef void RenderFunc (GtkTreeViewColumn*, GtkCellRenderer*, GtkTreeModel*, GtkTreeIter*, gpointer);
      static RenderFunc render_action;
      static RenderFunc render_state;
      static RenderFunc render_score;
      static RenderFunc render_bytes;
      static RenderFunc render_subject;

    private:
      Row* get_row (const Quark& message_id);

    private:
      void find_next_iterator_from (GtkTreeModel            * model,
                                    GtkTreeIter             * start_pos,
                                    const TreeIterFunctor   & iterate_func,
                                    const ArticleTester     & test_func,
                                    RowActionFunctor        & success_func,
                                    bool                      test_the_start_pos);
      void next_iterator (GtkTreeView            * view,
                          GtkTreeModel           * model,
                          const TreeIterFunctor  & iterate_func,
                          const ArticleTester    & test_func,
                          RowActionFunctor       & success_func);
  };
}

#endif
