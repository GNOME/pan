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
extern "C" {
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/general/debug.h>
#include <pan/tasks/task-xover.h>
#include <pan/icons/pan-pixbufs.h>
#include "actions.h"
#include "pad.h"
#include "gui.h"

#if !GTK_CHECK_VERSION(2,6,0)
#define GTK_STOCK_EDIT GTK_STOCK_OPEN
#endif

using pan::PanUI;

namespace
{
  PanUI * pan_ui (0);
  Prefs * prefs (0);

  struct BuiltinIconInfo
  {
    const guint8* raw;
    const char * name;
  };

  const BuiltinIconInfo my_builtin_icons [] =
  {
    { icon_article_read, "ICON_ARTICLE_READ" },
    { icon_article_unread, "ICON_ARTICLE_UNREAD" },
    { icon_compose_followup, "ICON_COMPOSE_FOLLOWUP" }, 
    { icon_compose_post, "ICON_COMPOSE_POST" }, 
    { icon_disk, "ICON_DISK" }, 
    { icon_filter_only_attachments, "ICON_ONLY_ATTACHMENTS" }, 
    { icon_filter_only_cached, "ICON_ONLY_CACHED" }, 
    { icon_filter_only_me, "ICON_ONLY_ME" }, 
    { icon_filter_only_unread, "ICON_ONLY_UNREAD" }, 
    { icon_filter_only_watched, "ICON_ONLY_WATCHED" }, 
    { icon_get_dialog, "ICON_GET_DIALOG" }, 
    { icon_get_selected, "ICON_GET_SELECTED" }, 
    { icon_get_subscribed, "ICON_GET_SUBSCRIBED" }, 
    { icon_read_group, "ICON_READ_GROUP" }, 
    { icon_read_more, "ICON_READ_MORE" }, 
    { icon_read_less, "ICON_READ_LESS" }, 
    { icon_read_unread_article, "ICON_READ_UNREAD_ARTICLE" }, 
    { icon_read_unread_thread, "ICON_READ_UNREAD_THREAD" }, 
    { icon_score, "ICON_SCORE" }, 
    { icon_search_pulldown, "ICON_SEARCH_PULLDOWN" }
  };

  void
  register_my_builtin_icons ()
  {
    GtkIconFactory * factory = gtk_icon_factory_new ();
    gtk_icon_factory_add_default (factory);

    for (int i(0), qty(G_N_ELEMENTS(my_builtin_icons)); i<qty; ++i)
    {
      GdkPixbuf * pixbuf = gdk_pixbuf_new_from_inline (-1, my_builtin_icons[i].raw, false, 0);

      const int width (gdk_pixbuf_get_width (pixbuf));
      gtk_icon_theme_add_builtin_icon (my_builtin_icons[i].name, width, pixbuf);

      GtkIconSet * icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
      gtk_icon_factory_add (factory, my_builtin_icons[i].name, icon_set);
      g_object_unref (pixbuf);

      //std::cerr << "registered icon " << my_builtin_icons[i].name << std::endl;

      gdk_pixbuf_unref (pixbuf);
      gtk_icon_set_unref (icon_set);
    }

    g_object_unref (G_OBJECT (factory));
  }

  GtkActionGroup * _group (0);

  void do_prompt_for_charset           (GtkAction * a) { pan_ui->do_prompt_for_charset(); }
  void do_read_selected_group          (GtkAction * a) { pan_ui->do_read_selected_group(); }
  void do_mark_selected_groups_read    (GtkAction * a) { pan_ui->do_mark_selected_groups_read(); }
  void do_clear_selected_groups        (GtkAction * a) { pan_ui->do_clear_selected_groups(); }
  void do_xover_selected_groups        (GtkAction * a) { pan_ui->do_xover_selected_groups(); }
  void do_xover_subscribed_groups      (GtkAction * a) { pan_ui->do_xover_subscribed_groups(); }
  void do_download_headers             (GtkAction * a) { pan_ui->do_download_headers(); }
  void do_refresh_groups               (GtkAction * a) { pan_ui->do_refresh_groups(); }
  void do_subscribe_selected_groups    (GtkAction * a) { pan_ui->do_subscribe_selected_groups(); }
  void do_unsubscribe_selected_groups  (GtkAction * a) { pan_ui->do_unsubscribe_selected_groups(); }
  void do_save_articles                (GtkAction * a) { pan_ui->do_save_articles(); }
  void do_print                        (GtkAction * a) { pan_ui->do_print(); }
  void do_import_tasks                 (GtkAction * a) { pan_ui->do_import_tasks(); }
  void do_cancel_latest_task           (GtkAction * a) { pan_ui->do_cancel_latest_task(); }
  void do_show_task_window             (GtkAction * a) { pan_ui->do_show_task_window(); }
  void do_show_log_window              (GtkAction * a) { pan_ui->do_show_log_window(); }
  void do_quit                         (GtkAction * a) { pan_ui->do_quit(); }
  void do_clear_header_pane            (GtkAction * a) { pan_ui->do_clear_header_pane(); }
  void do_clear_body_pane              (GtkAction * a) { pan_ui->do_clear_body_pane(); }
  void do_select_all_articles          (GtkAction * a) { pan_ui->do_select_all_articles(); }
  void do_unselect_all_articles        (GtkAction * a) { pan_ui->do_unselect_all_articles(); }
  void do_add_subthreads_to_selection  (GtkAction * a) { pan_ui->do_add_subthreads_to_selection(); }
  void do_add_threads_to_selection     (GtkAction * a) { pan_ui->do_add_threads_to_selection(); }
  void do_add_similar_to_selection     (GtkAction * a) { pan_ui->do_add_similar_to_selection(); }
  void do_select_article_body          (GtkAction * a) { pan_ui->do_select_article_body(); }
  void do_show_preferences_dialog      (GtkAction * a) { pan_ui->do_show_preferences_dialog(); }
  void do_show_group_preferences_dialog(GtkAction * a) { pan_ui->do_show_group_preferences_dialog(); }
  void do_show_profiles_dialog         (GtkAction * a) { pan_ui->do_show_profiles_dialog(); }
  void do_jump_to_group_tab            (GtkAction * a) { pan_ui->do_jump_to_group_tab(); }
  void do_jump_to_header_tab           (GtkAction * a) { pan_ui->do_jump_to_header_tab(); }
  void do_jump_to_body_tab             (GtkAction * a) { pan_ui->do_jump_to_body_tab(); }
  void do_rot13_selected_text          (GtkAction * a) { pan_ui->do_rot13_selected_text(); }
  void do_download_selected_article    (GtkAction * a) { pan_ui->do_download_selected_article(); }
  void do_read_selected_article        (GtkAction * a) { pan_ui->do_read_selected_article(); }
  void do_show_selected_article_info   (GtkAction * a) { pan_ui->do_show_selected_article_info(); }
  void do_read_more                    (GtkAction * a) { pan_ui->do_read_more(); }
  void do_read_less                    (GtkAction * a) { pan_ui->do_read_less(); }
  void do_read_next_unread_group       (GtkAction * a) { pan_ui->do_read_next_unread_group(); }
  void do_read_next_group              (GtkAction * a) { pan_ui->do_read_next_group(); }
  void do_read_next_unread_article     (GtkAction * a) { pan_ui->do_read_next_unread_article(); }
  void do_read_next_article            (GtkAction * a) { pan_ui->do_read_next_article(); }
  void do_read_next_watched_article    (GtkAction * a) { pan_ui->do_read_next_watched_article(); }
  void do_read_next_unread_thread      (GtkAction * a) { pan_ui->do_read_next_unread_thread(); }
  void do_read_next_thread             (GtkAction * a) { pan_ui->do_read_next_thread(); }
  void do_read_previous_article        (GtkAction * a) { pan_ui->do_read_previous_article(); }
  void do_read_previous_thread         (GtkAction * a) { pan_ui->do_read_previous_thread(); }
  void do_read_parent_article          (GtkAction * a) { pan_ui->do_read_parent_article(); }
  void do_show_servers_dialog          (GtkAction * a) { pan_ui->do_show_servers_dialog(); }
  void do_plonk                        (GtkAction * a) { pan_ui->do_plonk(); }
  void do_ignore                       (GtkAction * a) { pan_ui->do_ignore(); }
  void do_watch                        (GtkAction * a) { pan_ui->do_watch(); }
  void do_show_score_dialog            (GtkAction * a) { pan_ui->do_show_score_dialog(); }
  void do_show_new_score_dialog        (GtkAction * a) { pan_ui->do_show_new_score_dialog(); }
  void do_cancel_article               (GtkAction * a) { pan_ui->do_cancel_article(); }
  void do_supersede_article            (GtkAction * a) { pan_ui->do_supersede_article(); }
  void do_delete_article               (GtkAction * a) { pan_ui->do_delete_article(); }
  void do_clear_article_cache          (GtkAction * a) { pan_ui->do_clear_article_cache(); }
  void do_mark_article_read            (GtkAction * a) { pan_ui->do_mark_article_read(); }
  void do_mark_article_unread          (GtkAction * a) { pan_ui->do_mark_article_unread(); }
  void do_post                         (GtkAction * a) { pan_ui->do_post(); }
  void do_followup_to                  (GtkAction * a) { pan_ui->do_followup_to(); }
  void do_reply_to                     (GtkAction * a) { pan_ui->do_reply_to(); }
  void do_pan_web                      (GtkAction * a) { pan_ui->do_pan_web(); }
  void do_bug_report                   (GtkAction * a) { pan_ui->do_bug_report(); }
  void do_tip_jar                      (GtkAction * a) { pan_ui->do_tip_jar(); }
  void do_about_pan                    (GtkAction * a) { pan_ui->do_about_pan(); }

  void do_work_online         (GtkToggleAction * a) { pan_ui->do_work_online         (gtk_toggle_action_get_active(a)); }
  void do_tabbed_layout       (GtkToggleAction * a) { pan_ui->do_tabbed_layout       (gtk_toggle_action_get_active(a)); }
  void do_show_group_pane     (GtkToggleAction * a) { pan_ui->do_show_group_pane     (gtk_toggle_action_get_active(a)); }
  void do_show_header_pane    (GtkToggleAction * a) { pan_ui->do_show_header_pane    (gtk_toggle_action_get_active(a)); }
  void do_show_body_pane      (GtkToggleAction * a) { pan_ui->do_show_body_pane      (gtk_toggle_action_get_active(a)); }
  void do_show_toolbar        (GtkToggleAction * a) { pan_ui->do_show_toolbar        (gtk_toggle_action_get_active(a)); }
  void do_shorten_group_names (GtkToggleAction * a) { pan_ui->do_shorten_group_names (gtk_toggle_action_get_active(a)); }

  guint get_action_activate_signal_id ()
  {
    static guint sig_id = 0;
    if (!sig_id)
      sig_id = g_signal_lookup ("activate", GTK_TYPE_ACTION);
    return sig_id;
  }

  void
  toggle_action_set_active (const char * action_name, bool is_active)
  {
    GtkAction * a = gtk_action_group_get_action (_group, action_name);
    const guint sig_id (get_action_activate_signal_id ());
    const gulong tag (g_signal_handler_find (a, G_SIGNAL_MATCH_ID, sig_id, 0, NULL, NULL, NULL));
    g_signal_handler_block (a, tag);
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION(a), is_active);
    g_signal_handler_unblock (a, tag);
  };


  /**
  ***  Boy these `match only' buttons are more trouble to sync than they're worth. :)
  **/

  const int SCORE_STATE_MASK (MATCH_IGNORED | MATCH_LOW_SCORING | MATCH_NORMAL_SCORING | MATCH_MEDIUM_SCORING | MATCH_HIGH_SCORING | MATCH_WATCHED);
  int prev_score_state;
  int match_on_score_state (MATCH_LOW_SCORING | MATCH_NORMAL_SCORING | MATCH_MEDIUM_SCORING | MATCH_HIGH_SCORING | MATCH_WATCHED);

  void sync_score_state_actions ()
  {
    toggle_action_set_active ("match-only-watched-articles",    match_on_score_state == MATCH_WATCHED);
    toggle_action_set_active ("match-watched-articles",         match_on_score_state & MATCH_WATCHED);
    toggle_action_set_active ("match-high-scoring-articles",    match_on_score_state & MATCH_HIGH_SCORING);
    toggle_action_set_active ("match-medium-scoring-articles",  match_on_score_state & MATCH_MEDIUM_SCORING);
    toggle_action_set_active ("match-normal-scoring-articles",  match_on_score_state & MATCH_NORMAL_SCORING);
    toggle_action_set_active ("match-low-scoring-articles",     match_on_score_state & MATCH_LOW_SCORING);
    toggle_action_set_active ("match-ignored-articles",         match_on_score_state & MATCH_IGNORED);
  }

  void set_new_match_on_score_state (int new_state)
  {
    prev_score_state = match_on_score_state;
    match_on_score_state = new_state;
    sync_score_state_actions ();
    pan_ui->do_match_on_score_state (new_state);
  }

  void set_score_state_bit_from_toggle (GtkToggleAction * a, int bit)
  {
    int new_state (match_on_score_state);
    if (gtk_toggle_action_get_active (a))
      new_state |= bit;
    else
      new_state &= (SCORE_STATE_MASK & ~bit);
    if (!new_state)
      new_state = SCORE_STATE_MASK;
    set_new_match_on_score_state (new_state);
  }

  void do_match_only_watched_articles (GtkToggleAction * a)   { set_new_match_on_score_state (gtk_toggle_action_get_active(a) ? MATCH_WATCHED : prev_score_state); }
  void do_match_watched_articles (GtkToggleAction * a)        { set_score_state_bit_from_toggle (a, MATCH_WATCHED); }
  void do_match_high_scoring_articles (GtkToggleAction * a)   { set_score_state_bit_from_toggle (a, MATCH_HIGH_SCORING); }
  void do_match_medium_scoring_articles (GtkToggleAction * a) { set_score_state_bit_from_toggle (a, MATCH_MEDIUM_SCORING); }
  void do_match_normal_scoring_articles (GtkToggleAction * a) { set_score_state_bit_from_toggle (a, MATCH_NORMAL_SCORING); }
  void do_match_low_scoring_articles (GtkToggleAction * a)    { set_score_state_bit_from_toggle (a, MATCH_LOW_SCORING); }
  void do_match_ignored_articles (GtkToggleAction * a)        { set_score_state_bit_from_toggle (a, MATCH_IGNORED); }

  /**
  ***
  **/

  void do_match_only_cached_articles (GtkToggleAction * a) { pan_ui->do_match_only_cached_articles (gtk_toggle_action_get_active(a)); }
  void do_match_only_binary_articles (GtkToggleAction * a) { pan_ui->do_match_only_binary_articles (gtk_toggle_action_get_active(a)); }
  void do_match_only_my_articles (GtkToggleAction * a) { pan_ui->do_match_only_my_articles (gtk_toggle_action_get_active(a)); }
  void do_match_only_unread_articles (GtkToggleAction * a) { pan_ui->do_match_only_unread_articles (gtk_toggle_action_get_active(a)); }

  GtkActionEntry entries[] =
  {
    { "file-menu", NULL, N_("_File") },
    { "edit-menu", NULL, N_("_Edit") },
    { "view-layout-menu", NULL, N_("_Layout") },
    { "view-group-pane-menu", NULL, N_("_Group Pane") },
    { "view-header-pane-menu", NULL, N_("_Header Pane") },
    { "view-body-pane-menu", NULL, N_("_Body Pane") },
    { "view-menu", NULL, N_("_View") },
    { "filter-menu", NULL, N_("Filte_r") },
    { "go-menu", NULL, N_("_Go") },
    { "actions-menu", NULL, N_("_Actions") },
    { "article-actions-menu", NULL, N_("_Articles") },
    { "group-actions-menu", NULL, N_("G_roups") },
    { "posting-actions-menu", NULL, N_("_Post") },
    { "post-menu", NULL, N_("_Post") },
    { "help-menu", NULL, N_("_Help") },
    { "set-charset", NULL, N_("Set Character _Encoding..."), NULL, NULL, G_CALLBACK(do_prompt_for_charset) },
    { "read-selected-group", "ICON_READ_MORE", N_("_Read Group"), NULL, NULL, G_CALLBACK(do_read_selected_group) },
    { "mark-groups-read", GTK_STOCK_CLEAR, N_("_Mark Group _Read"), "<control><shift>M", NULL, G_CALLBACK(do_mark_selected_groups_read) },
    { "delete-groups-articles", GTK_STOCK_DELETE, N_("_Delete Group's Articles"), "<control><shift>Delete", NULL, G_CALLBACK(do_clear_selected_groups) },
    { "get-new-headers-in-selected-groups", "ICON_GET_SELECTED", N_("Get New _Headers in Selected Group"), "A", NULL, G_CALLBACK(do_xover_selected_groups) },
    { "get-new-headers-in-subscribed-groups", "ICON_GET_SUBSCRIBED", N_("Get New _Headers in Subscribed Groups"), "<shift>A", NULL, G_CALLBACK(do_xover_subscribed_groups) },
    { "download-headers", "ICON_GET_DIALOG", N_("Get _Headers..."), "", NULL, G_CALLBACK(do_download_headers) },
    { "refresh-group-list", NULL, N_("Refresh Group List"), "", NULL, G_CALLBACK(do_refresh_groups) },
    { "subscribe", NULL, N_("_Subscribe"), NULL, NULL, G_CALLBACK(do_subscribe_selected_groups) },
    { "unsubscribe", NULL, N_("_Unsubscribe"), NULL, NULL, G_CALLBACK(do_unsubscribe_selected_groups) },

    { "save-articles", GTK_STOCK_SAVE, N_("_Save Articles..."), "<shift>S", NULL, G_CALLBACK(do_save_articles) },
    { "print", GTK_STOCK_PRINT, NULL, "<control>P", NULL, G_CALLBACK(do_print) },
    { "import-tasks", GTK_STOCK_OPEN, N_("_Import NZB Files..."), NULL, NULL, G_CALLBACK(do_import_tasks) },
    { "cancel-last-task", GTK_STOCK_CANCEL, N_("_Cancel Last Task"), "<control>Delete", NULL, G_CALLBACK(do_cancel_latest_task) },
    { "show-task-window", NULL, N_("_Task Manager"), NULL, NULL, G_CALLBACK(do_show_task_window) },
    { "show-log-dialog", GTK_STOCK_DIALOG_INFO, N_("_Event Log"), NULL, NULL, G_CALLBACK(do_show_log_window) },
    { "quit", NULL, N_("_Quit"), "<control>Q", NULL, G_CALLBACK(do_quit) },

    { "select-all-articles", NULL, N_("Select _All Articles"), "<control>D", NULL, G_CALLBACK(do_select_all_articles) },
    { "unselect-all-articles", NULL, N_("_Deselect All Articles"), "<shift><control>D", NULL, G_CALLBACK(do_unselect_all_articles) },
    { "add-subthreads-to-selection", NULL, N_("Add Su_bthreads to Selection"), "D", NULL, G_CALLBACK(do_add_subthreads_to_selection) },
    { "add-threads-to-selection", NULL, N_("Add _Threads to Selection"), "<shift>D", NULL, G_CALLBACK(do_add_threads_to_selection) },
    { "add-similar-articles-to-selection", NULL, N_("Add _Similar Articles to Selection"), "<control>S", NULL, G_CALLBACK(do_add_similar_to_selection) },
    { "select-article-body", NULL, N_("Select Article _Body"), NULL, NULL, G_CALLBACK(do_select_article_body) },
    { "show-preferences-dialog", GTK_STOCK_PREFERENCES, N_("Edit _Preferences"), NULL, NULL, G_CALLBACK(do_show_preferences_dialog) },
    { "show-group-preferences-dialog", GTK_STOCK_PREFERENCES, N_("Edit _Group Preferences"), NULL, NULL, G_CALLBACK(do_show_group_preferences_dialog) },
    { "show-profiles-dialog", GTK_STOCK_EDIT, N_("Edit P_osting Profiles"), NULL, NULL, G_CALLBACK(do_show_profiles_dialog) },
    { "show-servers-dialog", GTK_STOCK_NETWORK, N_("Edit _News Servers"), NULL, NULL, G_CALLBACK(do_show_servers_dialog) },

    { "jump-to-group-tab", GTK_STOCK_JUMP_TO, N_("Jump to _Group Tab"), "1", NULL, G_CALLBACK(do_jump_to_group_tab) },
    { "jump-to-header-tab", GTK_STOCK_JUMP_TO, N_("Jump to _Header Tab"), "2", NULL, G_CALLBACK(do_jump_to_header_tab) },
    { "jump-to-body-tab", GTK_STOCK_JUMP_TO, N_("Jump to _Body Tab"), "3", NULL, G_CALLBACK(do_jump_to_body_tab) },
    { "rot13-selected-text", NULL, N_("_Rot13 Selected Text"), "<control><shift>R", NULL, G_CALLBACK(do_rot13_selected_text) },

    { "clear-header-pane", NULL, N_("Clear _Header Pane"), NULL, NULL, G_CALLBACK(do_clear_header_pane) },
    { "clear-body-pane", NULL, N_("Clear _Body Pane"), NULL, NULL, G_CALLBACK(do_clear_body_pane) },

    { "download-selected-article", "ICON_DISK", N_("Cache Article"), NULL, NULL, G_CALLBACK(do_download_selected_article) },
    { "read-selected-article", "ICON_READ_MORE", N_("Read Article"), NULL, NULL, G_CALLBACK(do_read_selected_article) },
    { "show-selected-article-info", NULL, N_("Show Article Information"), NULL, NULL, G_CALLBACK(do_show_selected_article_info) },
    { "read-more", "ICON_READ_MORE", N_("Read _More"), "space", NULL, G_CALLBACK(do_read_more) },
    { "read-less", "ICON_READ_LESS", N_("Read _Back"), "BackSpace", NULL, G_CALLBACK(do_read_less) },
    { "read-next-unread-group", "ICON_READ_UNREAD_GROUP", N_("Next _Unread Group"), "G", NULL, G_CALLBACK(do_read_next_unread_group) },
    { "read-next-group", "ICON_READ_GROUP", N_("Next _Group"), "<shift>G", NULL, G_CALLBACK(do_read_next_group) },
    { "read-next-unread-article", "ICON_READ_UNREAD_ARTICLE", N_("Next _Unread Article"), "N", NULL, G_CALLBACK(do_read_next_unread_article) },
    { "read-next-article", NULL, N_("Next _Article"), "<control>N", NULL, G_CALLBACK(do_read_next_article) },
    { "read-next-watched-article", NULL, N_("Next _Watched Article"), "<control><shift>N", NULL, G_CALLBACK(do_read_next_watched_article) },
    { "read-next-unread-thread", "ICON_READ_UNREAD_THREAD", N_("Next Unread _Thread"), "T", NULL, G_CALLBACK(do_read_next_unread_thread) },
    { "read-next-thread", NULL, N_("Next Threa_d"), "<control>T", NULL, G_CALLBACK(do_read_next_thread) },
    { "read-previous-article", NULL, N_("Pre_vious Article"), "V", NULL, G_CALLBACK(do_read_previous_article) },
    { "read-previous-thread", NULL, N_("Previous _Thread"), "<control>V", NULL, G_CALLBACK(do_read_previous_thread) },
    { "read-parent-article", NULL, N_("_Parent Article"), "U", NULL, G_CALLBACK(do_read_parent_article) },

    { "plonk", NULL, N_("Ignore _Author"), NULL, NULL, G_CALLBACK(do_plonk) },
    { "watch-thread", NULL, N_("_Watch Thread"), NULL, NULL, G_CALLBACK(do_watch) },
    { "ignore-thread", NULL, N_("_Ignore Thread"), NULL, NULL, G_CALLBACK(do_ignore) },
    { "view-article-score", "ICON_SCORE", N_("Edit Article's Watch/Ignore/Score..."), "<control><shift>C", NULL, G_CALLBACK(do_show_score_dialog) },
    { "add-article-score", "ICON_SCORE", N_("_Add a _Scoring Rule..."), "S", NULL, G_CALLBACK(do_show_new_score_dialog) },
    { "cancel-article", NULL, N_("Cance_l Article..."), NULL, NULL, G_CALLBACK(do_cancel_article) },
    { "supersede-article", NULL, N_("_Supersede Article..."), NULL, NULL, G_CALLBACK(do_supersede_article) },
    { "delete-article", GTK_STOCK_DELETE, N_("_Delete Article"), "Delete", NULL, G_CALLBACK(do_delete_article) },
    { "clear-article-cache", NULL, N_("Clear Article Cache"), NULL, NULL, G_CALLBACK(do_clear_article_cache) },
    { "mark-article-read", "ICON_ARTICLE_READ", N_("_Mark Article as Read"), "M", NULL, G_CALLBACK(do_mark_article_read) },
    { "mark-article-unread", "ICON_ARTICLE_UNREAD", N_("Mark Article as _Unread"), "<control>M", NULL, G_CALLBACK(do_mark_article_unread) },

    { "post", "ICON_COMPOSE_POST", N_("_Post to Newsgroup"), "P", NULL, G_CALLBACK(do_post) },
    { "followup-to", "ICON_COMPOSE_FOLLOWUP", N_("_Followup to Newsgroup"), "F", NULL, G_CALLBACK(do_followup_to) },
    { "reply-to", NULL, N_("_Reply to Author in Mail"), "R", NULL, G_CALLBACK(do_reply_to) },

    { "pan-web-page", NULL, N_("_Pan Home Page"), NULL, NULL, G_CALLBACK(do_pan_web) },
    { "bug-report", NULL, N_("Give _Feedback or Report a Bug..."), NULL, NULL, G_CALLBACK(do_bug_report) },
    { "tip-jar", NULL, N_("_Tip Jar..."), NULL, NULL, G_CALLBACK(do_tip_jar) },
#if GTK_CHECK_VERSION(2,6,0)
    { "about-pan", GTK_STOCK_ABOUT, N_("_About"), NULL, NULL, G_CALLBACK(do_about_pan) }
#else
    { "about-pan", NULL, N_("_About"), NULL, NULL, G_CALLBACK(do_about_pan) }
#endif
  };

  void prefs_toggle_callback_impl (GtkToggleAction * action)
  {
    const char * name (gtk_action_get_name (GTK_ACTION(action)));
    prefs->set_flag (name, gtk_toggle_action_get_active (action));
  }

  const guint n_entries (G_N_ELEMENTS (entries));

  GtkToggleActionEntry toggle_entries[] =
  {
    { "thread-headers",           NULL, N_("_Thread Headers"),                NULL, NULL, G_CALLBACK(prefs_toggle_callback_impl), true },
    { "wrap-article-body",        NULL, N_("_Wrap Article Body"),              "W", NULL, G_CALLBACK(prefs_toggle_callback_impl), false },
    { "mute-quoted-text",         NULL, N_("Mute _Quoted Text"),               "Q", NULL, G_CALLBACK(prefs_toggle_callback_impl), true },
    { "show-all-headers",         NULL, N_("Show All _Headers in Body Pane"),  "H", NULL, G_CALLBACK(prefs_toggle_callback_impl), false },
    { "show-smilies-as-graphics", NULL, N_("Show _Smilies as Graphics"),      NULL, NULL, G_CALLBACK(prefs_toggle_callback_impl), true },
    { "show-text-markup",         NULL, N_("Show *Bold*, __Underlined__, and /Italic/"), NULL, NULL, G_CALLBACK(prefs_toggle_callback_impl), true },
    { "size-pictures-to-fit",     NULL, N_("Size Pictures to _Fit"),          NULL, NULL, G_CALLBACK(prefs_toggle_callback_impl), true },
    { "monospace-font-enabled",   NULL, N_("Use _Monospace Font"),             "C", NULL, G_CALLBACK(prefs_toggle_callback_impl), false },
    { "focus-on-image",           NULL, N_("Set Focus to Images"),            NULL, NULL, G_CALLBACK(prefs_toggle_callback_impl), true },


    { "work-online", "ICON_ONLINE", N_("_Work Online"), "L", NULL, G_CALLBACK(do_work_online), true },
    { "tabbed-layout", GTK_STOCK_JUMP_TO, N_("_Tabbed Layout"), "Z", NULL, G_CALLBACK(do_tabbed_layout), false },
    { "show-group-pane", NULL, N_("Show Group _Pane"), "<control>1", NULL, G_CALLBACK(do_show_group_pane), true },
    { "show-header-pane", NULL, N_("Show Hea_der Pane"), "<control>2", NULL, G_CALLBACK(do_show_header_pane), true },
    { "show-body-pane", NULL, N_("Show Bod_y Pane"), "<control>3", NULL, G_CALLBACK(do_show_body_pane), true },
    { "show-toolbar", NULL, N_("Show _Toolbar"), NULL, NULL, G_CALLBACK(do_show_toolbar), true },
    { "shorten-group-names", GTK_STOCK_ZOOM_OUT, N_("Abbreviate Group Names"), "B", NULL, G_CALLBACK(do_shorten_group_names), false },

    { "match-only-unread-articles", "ICON_ONLY_UNREAD", N_("Match Only _Unread Articles"), NULL, NULL, G_CALLBACK(do_match_only_unread_articles), false },
    { "match-only-cached-articles", "ICON_ONLY_CACHED", N_("Match Only _Cached Articles"), NULL, NULL, G_CALLBACK(do_match_only_cached_articles), false },
    { "match-only-binary-articles", "ICON_ONLY_ATTACHMENTS", N_("Match Only _Complete Articles"), NULL, NULL, G_CALLBACK(do_match_only_binary_articles), false },
    { "match-only-my-articles", "ICON_ONLY_ME", N_("Match Only _My Articles"), NULL, NULL, G_CALLBACK(do_match_only_my_articles), false },
    { "match-only-watched-articles", "ICON_ONLY_WATCHED", N_("Match Only _Watched Articles"), NULL, NULL, G_CALLBACK(do_match_only_watched_articles), false },

    { "match-watched-articles", NULL, N_("Match Scores of 9999 (_Watched)"), NULL, NULL, G_CALLBACK(do_match_watched_articles), true },
    { "match-high-scoring-articles", NULL, N_("Match Scores of 5000...9998 (_High)"), NULL, NULL, G_CALLBACK(do_match_high_scoring_articles), true },
    { "match-medium-scoring-articles", NULL, N_("Match Scores of 1...4999 (Me_dium)"), NULL, NULL, G_CALLBACK(do_match_medium_scoring_articles), true },
    { "match-normal-scoring-articles", NULL, N_("Match Scores of 0 (_Normal)"), NULL, NULL, G_CALLBACK(do_match_normal_scoring_articles), true },
    { "match-low-scoring-articles", NULL, N_("Match Scores of -9998...-1 (_Low)"), NULL, NULL, G_CALLBACK(do_match_low_scoring_articles), true },
    { "match-ignored-articles", NULL, N_("Match Scores of -9999 (_Ignored)"), NULL, NULL, G_CALLBACK(do_match_ignored_articles), false }
  };

  const guint n_toggle_entries (G_N_ELEMENTS(toggle_entries));

  enum
  {
    SHOW_ARTICLES,
    SHOW_THREADS,
    SHOW_SUBTHREADS,
    SHOW_N
  };

  static const char * show_strings[SHOW_N] = { "articles", "threads", "subthreads" };

  void
  show_matches_cb (GtkAction *action, GtkRadioAction *current)
  {
    const int mode (gtk_radio_action_get_current_value (current));
    g_assert (0<=mode && mode<SHOW_N);
    prefs->set_string ("header-pane-show-matching", show_strings[mode]);
    pan_ui->do_show_matches (Data::ShowType(mode));
  }

  GtkRadioActionEntry match_toggle_entries[] =
  {
    { "show-matching-articles", NULL, N_("Show Matching _Articles"), NULL, NULL, Data::SHOW_ARTICLES },
    { "show-matching-threads", NULL, N_("Show Matching Articles' _Threads"), NULL, NULL, Data::SHOW_THREADS },
    { "show-matching-subthreads", NULL, N_("Show Matching Articles' _Subthreads"), NULL, NULL, Data::SHOW_SUBTHREADS }
  };

  std::map<std::string,GCallback> real_toggle_callbacks;

  /** Intercepts the toggle callbacks to update our prefs object,
      then routes the event on to the proper callback */
  void prefs_toggle_callback (GtkToggleAction * a)
  {
    prefs_toggle_callback_impl (a);
    typedef void (*GtkToggleActionCallback)(GtkToggleAction*);
    const char * name (gtk_action_get_name (GTK_ACTION(a)));
    ((GtkToggleActionCallback)(real_toggle_callbacks[name])) (a);
  }

  void ensure_tooltip (GtkActionEntry * e)
  {
    if (!e->tooltip && e->label)
    {
      std::string s = e->label;
      s.erase (std::remove (s.begin(), s.end(), '_'), s.end());
      e->tooltip = g_strdup (s.c_str());
    }
  }
}

void
pan :: add_actions (PanUI * ui, GtkUIManager * ui_manager, Prefs * p)
{
  pan_ui = ui;
  prefs = p;

  register_my_builtin_icons ();

  for (GtkToggleActionEntry *it(toggle_entries), *end(it+n_toggle_entries); it!=end; ++it)
  {
    ensure_tooltip (reinterpret_cast<GtkActionEntry*>(it));

    it->is_active = prefs->get_flag (it->name, it->is_active);
    if (it->callback != G_CALLBACK(prefs_toggle_callback_impl)) {
      real_toggle_callbacks[it->name] = it->callback;
      it->callback = G_CALLBACK(prefs_toggle_callback);
    }
  }

  const std::string show_str (p->get_string ("header-pane-show-matching", "articles"));
  int show_mode (SHOW_ARTICLES);
  for (int i=0; i<SHOW_N; ++i) {
    if (show_str == show_strings[i]) {
      show_mode = i;
      break;
    }
  }

  for (GtkActionEntry *it(entries), *end(it+n_entries); it!=end; ++it)
    ensure_tooltip (it);

  GtkActionGroup * action_group = _group = gtk_action_group_new ("Actions");
  gtk_action_group_set_translation_domain (action_group, NULL);
  gtk_action_group_add_actions (action_group, entries, n_entries, NULL);
  gtk_action_group_add_toggle_actions (action_group, toggle_entries, n_toggle_entries, NULL);
  gtk_action_group_add_radio_actions (action_group,
                                      match_toggle_entries, G_N_ELEMENTS(match_toggle_entries),
                                      show_mode,
                                      G_CALLBACK(show_matches_cb), NULL);
  gtk_ui_manager_insert_action_group (ui_manager, action_group, 0);
  g_object_unref (G_OBJECT(action_group));
}
