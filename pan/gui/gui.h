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
#ifndef _Pan_h_
#define _Pan_h_

#include <pan/general/log.h>
#include <pan/general/locking.h>
#include <pan/general/progress.h>
#include <pan/data/article-cache.h>
#include <pan/data/encode-cache.h>
#include <pan/tasks/queue.h>
#include <pan/data/article.h>
#include <pan/data/cert-store.h>
#include <pan/gui/action-manager.h>
#include <pan/gui/pan-ui.h>
#include <pan/gui/prefs.h>
#include <pan/gui/group-prefs.h>
#include <pan/gui/wait.h>

#include <memory>
#include <stdint.h>

#include <gtk/gtk.h>
#include <gdk/gdk.h>

namespace pan
{
  class EditorSpawner;
  class GroupPane;
  class HeaderPane;
  class BodyPane;
  class ProgressView;

  /**
   * The main GUI object for Pan's GTK frontend
   * @ingroup GUI
   */
  struct GUI final:
    public virtual PanUI,
    public ActionManager,
    public WaitUI,
    private Log::Listener,
    private Progress::Listener,
    private Queue::Listener,
    private Prefs::Listener,
    private CertStore::Listener,
    private Data::Listener
  {

    public:

      typedef std::vector<Quark> mid_sequence_t;

      GUI (Data& data, Queue&, Prefs&, GroupPrefs&);
      virtual ~GUI ();

      GUI(GUI const &) = delete;
      GUI &operator=(GUI const &) = delete;

      GtkWidget* root () { return _root; }
      typedef std::vector<std::string> strings_t;

      struct VerifyData
      {
#ifdef HAVE_GNUTLS
        gnutls_x509_crt_t cert;
#endif
        std::string server;
        std::string cert_name;
        int nr;
        GUI* gui;
#ifdef HAVE_GNUTLS
        void deinit_cert() { gnutls_x509_crt_deinit(cert); }
#endif
      };

    public: // ActionManager
      bool is_action_active (const char * action_name) const override;
      void activate_action (const char * action_name) const override;
      void toggle_action (const char * action_name, bool) const override;
      void sensitize_action (const char * action_name, bool) const override;
      void hide_action (const char * key, bool b) const override;
      GtkWidget* get_action_widget (const char * key) const override;
      void disable_accelerators_when_focused (GtkWidget * entry) const override;

    public: // Prefs::Listener
      void on_prefs_flag_changed   (const StringView& key, bool value) override;
      void on_prefs_int_changed    (const StringView&,     int) override { }
      void on_prefs_string_changed (const StringView& key, const StringView& value) override;
      void on_prefs_color_changed  (const StringView&,     const GdkRGBA&) override { }

    public: // PanUI
      void do_prompt_for_charset () override;
      void do_save_articles () override;
      void do_save_articles_from_nzb () override;
      void do_save_articles_to_nzb () override;
      void do_print () override;
      void do_quit () override;
      void do_import_tasks () override;
      void do_import_tasks_from_nzb_stream (const char*) override;
      void do_cancel_latest_task () override;
      void do_show_task_window () override;
      void do_show_log_window () override;
      void do_select_all_articles () override;
      void do_unselect_all_articles () override;
      void do_add_similar_to_selection () override;
      void do_add_threads_to_selection () override;
      void do_add_subthreads_to_selection () override;
      void do_select_article_body () override;
      void do_show_preferences_dialog () override;
      void do_show_group_preferences_dialog () override;
      void do_show_profiles_dialog () override;
      void do_jump_to_group_tab () override;
      void do_jump_to_header_tab () override;
      void do_jump_to_body_tab () override;
      void do_rot13_selected_text () override;
      void do_download_selected_article () override;
      void do_clear_header_pane () override;
      void do_clear_body_pane () override;
      void do_read_selected_article () override;
      void do_read_more () override;
      void do_read_less () override;
      void do_read_next_unread_group () override;
      void do_read_next_group () override;
      void do_read_next_unread_article () override;
      void do_read_next_article () override;
      void do_read_next_watched_article () override;
      void do_read_next_unread_thread () override;
      void do_read_next_thread () override;
      void do_read_previous_article () override;
      void do_read_previous_thread () override;
      void do_read_parent_article () override;
      void do_show_servers_dialog () override;
      void do_show_sec_dialog () override;
      void do_collapse_thread () override;
      void do_expand_thread () override;
      void do_show_selected_article_info () override;
      void do_plonk () override;
      void do_watch () override;
      void do_ignore () override;
      void do_flag () override;
      void do_flag_off () override;
      void do_next_flag () override;
      void do_last_flag () override;
      void do_mark_all_flagged () override;
      void do_invert_selection () override;
      void do_show_score_dialog () override;
      void do_show_new_score_dialog () override;
      void do_cancel_article () override;
      void do_supersede_article () override;
      void do_delete_article () override;
      void do_clear_article_cache () override;
      void do_mark_article_read () override;
      void do_mark_article_unread () override;
      void do_mark_thread_read () override;
      void do_mark_thread_unread () override;
      void do_post () override;
      void do_followup_to () override;
      void do_reply_to () override;
#ifdef HAVE_MANUAL
      void do_pan_manual () override;
#endif
      void do_pan_web () override;
      void do_bug_report () override;
      void do_about_pan () override;
      void do_work_online (bool) override final;
      void do_layout (bool) override final;
      void do_show_toolbar (bool) override;
      void do_show_group_pane (bool) override;
      void do_show_header_pane (bool) override;
      void do_show_body_pane (bool) override;
      void do_shorten_group_names (bool) override;
      void do_match_only_cached_articles (bool) override;
      void do_match_only_binary_articles (bool) override;
      void do_match_only_my_articles (bool) override;
      void do_match_only_unread_articles (bool) override;
      void do_match_only_read_articles (bool) override;
      void do_enable_toggle_rules (bool enable) override;
      void do_match_on_score_state (int) override;
      void do_show_matches (const Data::ShowType) override;
      void do_read_selected_group () override;
      void do_mark_selected_groups_read () override;
      void do_clear_selected_groups () override;
      void do_xover_selected_groups () override;
      void do_xover_subscribed_groups () override;
      void do_download_headers () override;
      void do_refresh_groups () override;
      void do_subscribe_selected_groups () override;
      void do_unsubscribe_selected_groups () override;
      void do_edit_scores(GtkAction *) override;
#ifdef HAVE_GNUTLS
      void do_show_cert_failed_dialog(VerifyData* data);
      bool confirm_accept_new_cert_dialog(GtkWindow*, gnutls_x509_crt_t, const Quark&);
#endif
      void step_bookmarks(int step);
      void do_read_or_save_articles ();

    public:
      static std::string prompt_user_for_save_path (GtkWindow * parent, const Prefs& prefs);
      static std::string prompt_user_for_filename  (GtkWindow * parent, const Prefs& prefs);

    private: // Queue::Listener
      friend class Queue;
      void on_queue_task_active_changed (Queue&, Task&, bool active) override final;
      void on_queue_tasks_added (Queue&, int index UNUSED, int count UNUSED) override { }
      void on_queue_task_removed (Queue&, Task&, int pos UNUSED) override { }
      void on_queue_task_moved (Queue&, Task&, int new_pos UNUSED, int old_pos UNUSED) override { }
      void on_queue_connection_count_changed (Queue&, int count) override;
      void on_queue_size_changed (Queue&, int active, int total) override;
      void on_queue_online_changed (Queue&, bool online) override;
      void on_queue_error (Queue&, const StringView& message) override;
#ifdef HAVE_GNUTLS
    private:  // CertStore::Listener
      void on_verify_cert_failed(gnutls_x509_crt_t, std::string, int) override;
      void on_valid_cert_added (gnutls_x509_crt_t, std::string) override;
#endif
    private: // Log::Listener
      void on_log_entry_added (const Log::Entry& e) override;
      void on_log_cleared () override {}

    private: // Progress::Listener
      void on_progress_finished (Progress&, int status) override;

    public: // WaitUI
      void watch_cursor_on () override;
      void watch_cursor_off () override;

    private:
      void set_selected_thread_score (int score);
      bool deletion_confirmation_dialog() const;

    private:
      Data& _data;
      Queue& _queue;
      ArticleCache& _cache;
      EncodeCache& _encode_cache;
      Prefs& _prefs;
      GroupPrefs& _group_prefs;
      CertStore& _certstore;

    private:
      GtkWidget * _root;
      GtkWidget * _menu_vbox;
      GtkWidget * _workarea_bin;
      GtkWidget * _toolbar;
      GroupPane * _group_pane;
      HeaderPane * _header_pane;
      BodyPane * _body_pane;
      GtkUIManager * _ui_manager;

      GtkWidget * _info_image;
      GtkWidget * _error_image;

      GtkWidget * _connection_size_eventbox;
      GtkWidget * _connection_size_label;
      GtkWidget * _queue_size_label;
      GtkWidget * _queue_size_button;
      GtkWidget * _event_log_button;
      GtkWidget * _taskbar;
      std::vector<ProgressView*> _views;
      ProgressView* _meter_view;
      std::list<Task*> _active_tasks;
      std::string _charset;

      GtkWidget* _meter_button;

      void set_charset (const StringView& v);

      void upkeep ();
      guint upkeep_tag;
      static int upkeep_timer_cb (gpointer gui_g);
      void set_queue_size_label (unsigned int running, unsigned int size);
      void refresh_connection_label ();

      static void show_event_log_cb (GtkWidget*, gpointer);
      static void show_task_window_cb (GtkWidget*, gpointer);
      static void show_download_meter_prefs_cb (GtkWidget*, gpointer);

      void score_add (int);

      static void notebook_page_switched_cb (GtkNotebook*, void *, gint, gpointer);

      static void root_realized_cb (GtkWidget*, gpointer self_gpointer);

    public:
      BodyPane* body_pane() { return _body_pane; }

      GtkWidget* meter_button () { return _meter_button; }

    private:
      static void add_widget (GtkUIManager*, GtkWidget*, gpointer);
      static void server_list_dialog_destroyed_cb (GtkWidget*, gpointer);
      void server_list_dialog_destroyed (GtkWidget*);
      static void sec_dialog_destroyed_cb (GtkWidget*, gpointer);
      void sec_dialog_destroyed (GtkWidget*);
      static void prefs_dialog_destroyed_cb (GtkWidget * w, gpointer self);
      void prefs_dialog_destroyed (GtkWidget* w);
      int score_int_from_string(std::string val, const char* rules[]);

      void edit_scores_cleanup(int, char *, GtkAction *);
      std::unique_ptr<EditorSpawner> _spawner;

#ifdef HAVE_GNUTLS
      static gboolean show_cert_failed_cb(gpointer gp);
#endif
    public:
      GtkUIManager* get_ui_manager() { return _ui_manager; }
  };
}

#endif /* __PAN_H__ */
