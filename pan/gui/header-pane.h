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

#ifndef _HeaderPane_h_
#define _HeaderPane_h_

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include <pan/general/e-util.h>
#include <pan/general/macros.h> // for UNUSED
#include <pan/gui/action-manager.h>
#include <pan/gui/group-prefs.h>
#include <pan/gui/gui.h>
#include <pan/gui/pan-tree.h>
#include <pan/gui/prefs.h>
#include <pan/gui/wait.h>
#include <pan/tasks/queue.h>
#include <pan/usenet-utils/filter-info.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/usenet-utils/rules-info.h>

namespace pan {
/**
 * Base class for functors taking a const Article& and returning bool.
 * @ingroup GUI
 */
struct ArticleTester
{
    virtual ~ArticleTester()
    {
    }

    virtual bool operator()(Article const &) const = 0;
};

/**
 * Base class for tree navigation functors
 * @ingroup GUI
 */
struct TreeIterFunctor
{
    virtual ~TreeIterFunctor()
    {
    }

    virtual bool operator()(GtkTreeModel *, GtkTreeIter *) const = 0;
    virtual bool front(GtkTreeModel *, GtkTreeIter *) const = 0;
};

/**
 * Base class for actions performed on a header pane row.
 */
struct RowActionFunctor
{
    virtual ~RowActionFunctor()
    {
    }

    virtual void operator()(GtkTreeModel *model,
                            GtkTreeIter *iter,
                            Article const &a) = 0;
};

typedef std::vector<Article const *> articles_t;
typedef std::set<Article const *> articles_set;
typedef std::vector<Article const *> article_v;

/**
 * Header Pane in the main window of Pan's GUI.
 * @ingroup GUI
 */
class HeaderPane :
  private Data::Listener,
  private Data::ArticleTree::Listener,
  private Prefs::Listener,
  private Queue::Listener,
  private ArticleCache::Listener
{
  public:
    HeaderPane(ActionManager &,
               Data &data,
               Queue &,
               ArticleCache &,
               Prefs &,
               GroupPrefs &,
               WaitUI &,
               GUI &);
    ~HeaderPane();

  public:
    void refilter();
    void set_show_type(const Data::ShowType);

  public:
    void set_focus();
    void unselect_all();
    void select_all();
    void select_threads();
    void select_subthreads();
    void select_similar();
    void expand_selected();
    void collapse_selected();

  public:
    void read_next_article();
    void read_next_unread_article();
    void read_next_thread();
    void read_next_unread_thread();
    void read_previous_article();
    void read_previous_thread();
    void read_parent_article();

    void mark_all_flagged();
    void invert_selection();
    void move_to_next_bookmark(int);

  private:
    void action_next_if(ArticleTester const &test, RowActionFunctor &action);
    void read_next_if(ArticleTester const &);
    void read_prev_if(ArticleTester const &);
    void select_next_if(ArticleTester const &test);
    void select_prev_if(ArticleTester const &test);

  public:
    GtkWidget *root()
    {
      return _root;
    }

    GtkWidget *get_default_focus_widget()
    {
      return _tree_view;
    }

    GtkWidget *create_filter_entry();
    Article const *get_first_selected_article() const;
    Article *get_first_selected_article();
    std::set<Article const *> get_full_selection() const;
    std::vector<Article const *> get_full_selection_v() const;
    const guint get_full_selection_rows_num() const;
    std::set<Article const *> get_nested_selection(bool do_mark_all) const;
    bool set_group(Quark const &group);

    Quark const &get_group()
    {
      return _group;
    }

  private:
    void select_message_id(Quark const &mid, bool do_scroll = true);

  private:
    void rebuild();
    void rebuild_article_action(Quark const &message_id);
    void rebuild_article_state(Quark const &message_id);
    void rebuild_all_article_states();

  private:
    void on_group_read(Quark const &group) override;

  private:
    virtual void on_tree_change(Data::ArticleTree::Diffs const &);

  private:
    void on_prefs_flag_changed(StringView const &, bool) override;

    void on_prefs_int_changed(StringView const &, int) override
    {
    }

    void on_prefs_string_changed(StringView const &,
                                 StringView const &) override;
    void on_prefs_color_changed(StringView const &, GdkRGBA const &) override;

  public:
    void on_article_flag_changed(articles_t &a, Quark const &group) override;

  private:
    void on_queue_task_active_changed(Queue &,
                                      Task &,
                                      bool active UNUSED) override
    {
    }

    void on_queue_tasks_added(Queue &, int index, int count) override;
    void on_queue_task_removed(Queue &, Task &, int index) override;

    void on_queue_task_moved(Queue &,
                             Task &,
                             int new_index UNUSED,
                             int old_index UNUSED) override
    {
    }

    void on_queue_connection_count_changed(Queue &, int count UNUSED) override
    {
    }

    void on_queue_size_changed(Queue &,
                               int active UNUSED,
                               int total UNUSED) override
    {
    }

    void on_queue_online_changed(Queue &, bool online UNUSED) override
    {
    }

    void on_queue_error(Queue &, StringView const &message UNUSED) override
    {
    }

  private:
    void on_cache_added(Quark const &mid) override;
    void on_cache_removed(quarks_t const &mids) override;

  public: // pretend it's private
    ActionManager &_action_manager;

  private:
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

    class Row : public PanTreeStore::Row
    {
      public:
        Article const *article;
        char *date_str;
        Quark short_author;
        int action;
        int state;
        bool is_read;

      private:
        static Quark build_short_author(Article const *article)
        {
          return Quark(GNKSA ::get_short_author_name(article->author.c_str()));
        }

      private:
        mutable char *collated_subject;
        mutable char *collated_author;

        // casefolded utf-8-friendly comparison key...
        static char *do_collate(StringView const &view)
        {
          StringView in(view);
          while (! in.empty())
          {
            const gunichar ch = g_utf8_get_char(in.str);
            if (! g_unichar_isalnum(
                  ch)) // eat everything before the first alpha
            {
              in.eat_chars(g_unichar_to_utf8(ch, NULL));
            }
            else if (in.len >= 3 // Re: reply leader
                     && (in.str[0] == 'R' || in.str[0] == 'r')
                     && (in.str[1] == 'E' || in.str[1] == 'e')
                     && in.str[2] == ':')
            {
              in.eat_chars(3);
            }
            else
            {
              break;
            }
          }

          if (in.empty())
          {
            return g_strdup("");
          }

          char *casefold = g_utf8_casefold(in.str, in.len);
          char *ret = g_utf8_collate_key(casefold, -1);
          g_free(casefold);
          return ret;
        }

      public:
        // lazy instantation... it's expensive and user might never sort by this
        // key
        char *get_collated_subject() const
        {
          if (! collated_subject)
          {
            collated_subject = do_collate(article->subject.to_view());
          }
          return collated_subject;
        }

        // lazy instantation... it's expensive and user might never sort by this
        // key
        char *get_collated_author() const
        {
          if (! collated_author)
          {
            collated_author = do_collate(short_author.to_view());
          }
          return collated_author;
        }

      public:
        Row(Data const &data,
            Article const *a,
            char *date_str_in,
            int action_in,
            int state_in) :
          article(a),
          date_str(date_str_in),
          short_author(build_short_author(a)),
          action(action_in),
          state(state_in),
          is_read(data.is_read(a)),
          collated_subject(nullptr),
          collated_author(nullptr)
        {
        }

        virtual ~Row() override
        {
          g_free(collated_subject);
          g_free(collated_author);
          g_free(date_str);
        }

      public:
        virtual void get_value(int column, GValue *setme) override
        {
          switch (column)
          {
            case COL_DATE_STR:
              set_value_static_string(setme, date_str);
              break;
            case COL_STATE:
              set_value_int(setme, state);
              break;
            case COL_ACTION:
              set_value_int(setme, action);
              break;
            case COL_SCORE:
              set_value_int(setme, article->score);
              break;
            case COL_LINES:
              set_value_ulong(setme, article->get_line_count());
              break;
            case COL_BYTES:
              set_value_ulong(setme, article->get_byte_count());
              break;
            case COL_DATE:
              set_value_ulong(setme, (unsigned long)article->time_posted);
              break;
            case COL_ARTICLE_POINTER:
              set_value_pointer(setme, (void *)article);
              break;
            case COL_SUBJECT:
              set_value_static_string(setme, article->author.c_str());
              break;
            case COL_SHORT_AUTHOR:
              set_value_static_string(setme, short_author.c_str());
              break;
          }
        }
    };

    struct RowLessThan
    {
        bool operator()(Row const *a, Row const *b) const
        {
          return a->article->message_id < b->article->message_id;
        }

        bool operator()(Row const *a, Quark const &b) const
        {
          return a->article->message_id < b;
        }

        bool operator()(Quark const &a, Row const *b) const
        {
          return a < b->article->message_id;
        }
    };

    typedef sorted_vector<Row *, true, RowLessThan> mid_to_row_t;

    mid_to_row_t _mid_to_row;

  private:
    static Article const *get_article(GtkTreeModel *, GtkTreeIter *);
    int find_highest_followup_score(GtkTreeModel *, GtkTreeIter *) const;
    Row *create_row(EvolutionDateMaker const &, Article const *);
    void add_children_to_model(PanTreeStore *store,
                               PanTreeStore::Row *parent_row,
                               Quark const &parent_mid,
                               Data::ArticleTree const *atree,
                               EvolutionDateMaker const &date_maker,
                               bool const do_thread);
    PanTreeStore *build_model(Quark const &,
                              Data::ArticleTree *,
                              TextMatch const *);
    void build_tree_columns();

  private:
    Data &_data;
    Queue &_queue;
    Prefs &_prefs;
    GroupPrefs &_group_prefs;
    WaitUI &_wait;
    Quark _group;
    Data::ArticleTree *_atree;
    GtkWidget *_root;
    GtkWidget *_tree_view;
    PanTreeStore *_tree_store;
    FilterInfo _filter;
    RulesInfo _rules;
    Data::ShowType _show_type;
    guint _selection_changed_idle_tag;

    // default text colors, updated on prefs change
    std::string _fg;
    std::string _bg;

  private:
    void rebuild_filter(std::string const &, int);
    void rebuild_rules(bool enable = false);
    std::pair<int, int> get_int_from_rules_str(std::string val);
    void refresh_font();

  public: // public so that anonymous namespace can reach -- don't call
    void filter(std::string const &text, int mode);
    void rules(bool enable = false);
    static void do_popup_menu(GtkWidget *, GdkEventButton *, gpointer);
    static void on_row_activated(GtkTreeView *,
                                 GtkTreePath *,
                                 GtkTreeViewColumn *,
                                 gpointer);
    static gboolean on_button_pressed(GtkWidget *, GdkEventButton *, gpointer);
    static gboolean on_keyboard_button_pressed(GtkWidget *widget,
                                               GdkEventKey *event,
                                               gpointer data);
    ArticleCache &_cache;
    GUI &_gui;

  private:
    void get_nested_foreach(GtkTreeModel *,
                            GtkTreePath *,
                            GtkTreeIter *,
                            gpointer) const;
    static void get_nested_foreach_static(GtkTreeModel *,
                                          GtkTreePath *,
                                          GtkTreeIter *,
                                          gpointer);
    static void get_full_selection_v_foreach(GtkTreeModel *,
                                             GtkTreePath *,
                                             GtkTreeIter *,
                                             gpointer);

  private:
    static int column_compare_func(GtkTreeModel *,
                                   GtkTreeIter *,
                                   GtkTreeIter *,
                                   gpointer);

  private:
    class CountUnread;
    class RowInserter;
    class SimilarWalk;
    void walk_and_collect(GtkTreeModel *, GtkTreeIter *, articles_set &) const;
    void walk_and_collect_flagged(GtkTreeModel *,
                                  GtkTreeIter *,
                                  GtkTreeSelection *) const;
    void walk_and_invert_selection(GtkTreeModel *,
                                   GtkTreeIter *,
                                   GtkTreeSelection *) const;

  private:
    typedef void RenderFunc(GtkTreeViewColumn *,
                            GtkCellRenderer *,
                            GtkTreeModel *,
                            GtkTreeIter *,
                            gpointer);
    static RenderFunc render_action;
    static RenderFunc render_state;
    static RenderFunc render_score;
    static RenderFunc render_bytes;
    static RenderFunc render_subject;
    static RenderFunc render_author;
    static RenderFunc render_lines;
    static RenderFunc render_date;

  private:
    Row *get_row(Quark const &message_id);

  private:
    static void on_selection_changed(GtkTreeSelection *, gpointer);
    static gboolean on_selection_changed_idle(gpointer);
    static void sort_column_changed_cb(GtkTreeSortable *sortable,
                                       gpointer user_data);

  private:
    void find_next_iterator_from(GtkTreeModel *model,
                                 GtkTreeIter *start_pos,
                                 TreeIterFunctor const &iterate_func,
                                 ArticleTester const &test_func,
                                 RowActionFunctor &success_func,
                                 bool test_the_start_pos);
    void next_iterator(GtkTreeView *view,
                       GtkTreeModel *model,
                       TreeIterFunctor const &iterate_func,
                       ArticleTester const &test_func,
                       RowActionFunctor &success_func);

  private:
    bool _cleared;

  public:
    void clear()
    {
      set_group(Quark());
      set_cleared(true);
    }

    void set_cleared(bool val)
    {
      _cleared = val;
    }

    bool get_cleared()
    {
      return _cleared;
    }
};
} // namespace pan

#endif
