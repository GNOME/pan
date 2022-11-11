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

#ifndef __pan_ui_h__
#define __pan_ui_h__

namespace pan
{
  /**
   * Interface class for Pan GUI actions.
   * @ingroup GUI
   */
  struct PanUI
  {
    PanUI() {}
    virtual ~PanUI() {}
    virtual void do_prompt_for_charset () = 0;
    virtual void do_save_articles () = 0;
    virtual void do_save_articles_from_nzb () = 0;
    virtual void do_save_articles_to_nzb () = 0;
    virtual void do_print () = 0;
    virtual void do_import_tasks () = 0;
    virtual void do_import_tasks_from_nzb_stream (const char*) = 0;
    virtual void do_cancel_latest_task () = 0;
    virtual void do_select_all_articles () = 0;
    virtual void do_unselect_all_articles () = 0;
    virtual void do_add_similar_to_selection () = 0;
    virtual void do_add_threads_to_selection () = 0;
    virtual void do_add_subthreads_to_selection () = 0;
    virtual void do_select_article_body () = 0;
    virtual void do_jump_to_group_tab () = 0;
    virtual void do_jump_to_header_tab () = 0;
    virtual void do_jump_to_body_tab () = 0;
    virtual void do_rot13_selected_text () = 0;
    virtual void do_download_selected_article () = 0;
    virtual void do_clear_header_pane () = 0;
    virtual void do_clear_body_pane () = 0;
    virtual void do_read_selected_article () = 0;
    virtual void do_read_more () = 0;
    virtual void do_read_less () = 0;
    virtual void do_read_next_unread_group () = 0;
    virtual void do_read_next_group () = 0;
    virtual void do_read_next_unread_article () = 0;
    virtual void do_read_next_article () = 0;
    virtual void do_read_next_watched_article () = 0;
    virtual void do_read_next_unread_thread () = 0;
    virtual void do_read_next_thread () = 0;
    virtual void do_read_previous_article () = 0;
    virtual void do_read_previous_thread () = 0;
    virtual void do_read_parent_article () = 0;
    virtual void do_plonk () = 0;
    virtual void do_watch () = 0;
    virtual void do_ignore () = 0;
    virtual void do_flag () = 0;
    virtual void do_flag_off () = 0;
    virtual void do_next_flag () = 0;
    virtual void do_last_flag () = 0;
    virtual void do_mark_all_flagged () = 0;
    virtual void do_invert_selection () = 0;
    virtual void do_cancel_article () = 0;
    virtual void do_supersede_article () = 0;
    virtual void do_delete_article () = 0;
    virtual void do_clear_article_cache () = 0;
    virtual void do_mark_article_read () = 0;
    virtual void do_mark_article_unread () = 0;
    virtual void do_mark_thread_read () = 0;
    virtual void do_mark_thread_unread () = 0;
    virtual void do_post () = 0;
    virtual void do_followup_to () = 0;
    virtual void do_reply_to () = 0;
#ifdef HAVE_MANUAL
    virtual void do_pan_manual () = 0;
#endif
    virtual void do_pan_web () = 0;
    virtual void do_bug_report () = 0;
    virtual void do_about_pan () = 0;
    virtual void do_quit () = 0;

    virtual void do_show_task_window () = 0;
    virtual void do_show_log_window () = 0;
    virtual void do_show_preferences_dialog () = 0;
    virtual void do_show_group_preferences_dialog () = 0;
    virtual void do_show_profiles_dialog () = 0;
    virtual void do_show_servers_dialog () = 0;
    virtual void do_show_sec_dialog () = 0;
    virtual void do_show_score_dialog () = 0;
    virtual void do_show_new_score_dialog () = 0;
    virtual void do_show_selected_article_info () = 0;

    virtual void do_collapse_thread () = 0;
    virtual void do_expand_thread () = 0;

    virtual void do_read_selected_group () = 0;
    virtual void do_mark_selected_groups_read () = 0;
    virtual void do_clear_selected_groups () = 0;
    virtual void do_xover_selected_groups () = 0;
    virtual void do_xover_subscribed_groups () = 0;
    virtual void do_download_headers () = 0;
    virtual void do_refresh_groups () = 0;
    virtual void do_subscribe_selected_groups () = 0;
    virtual void do_unsubscribe_selected_groups () = 0;

    virtual void do_work_online (bool) = 0;
    virtual void do_layout (bool) = 0;

    virtual void do_show_group_pane (bool) = 0;
    virtual void do_show_header_pane (bool) = 0;
    virtual void do_show_body_pane (bool) = 0;
    virtual void do_show_toolbar (bool) = 0;
    virtual void do_shorten_group_names (bool) = 0;

    virtual void do_match_only_read_articles (bool) = 0;
    virtual void do_match_only_unread_articles (bool) = 0;
    virtual void do_match_only_cached_articles (bool) = 0;
    virtual void do_match_only_binary_articles (bool) = 0;
    virtual void do_match_only_my_articles (bool) = 0;
    virtual void do_show_matches (const Data::ShowType) = 0;

    virtual void do_enable_toggle_rules (bool enable) = 0;

#define MATCH_IGNORED         (1<<0)
#define MATCH_LOW_SCORING     (1<<1)
#define MATCH_NORMAL_SCORING  (1<<2)
#define MATCH_MEDIUM_SCORING  (1<<3)
#define MATCH_HIGH_SCORING    (1<<4)
#define MATCH_WATCHED         (1<<5)
    virtual void do_match_on_score_state (int) = 0;

    virtual void do_edit_scores(GtkAction *) = 0;
  };
}

#endif
