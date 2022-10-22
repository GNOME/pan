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

#ifndef __PostUI_h__
#define __PostUI_h__

#include <gmime/gmime-message.h>
#include <pan/gui/prefs.h>
#include <pan/general/progress.h>
#include <pan/tasks/queue.h>
#include <pan/tasks/upload-queue.h>
#include <pan/usenet-utils/text-massager.h>
#include <pan/data/encode-cache.h>
#include "group-prefs.h"

#include <memory>

namespace pan
{
  class EditorSpawner;
  class Profiles;
  class TaskPost;
  class UploadQueue;
  class Queue;

  /**
   * Dialog for posting new messages Pan's GTK GUI.
   * @ingroup GUI
   */
  class PostUI: private Progress::Listener,
                private UploadQueue::Listener
  {
    public:

      typedef std::vector<Task*> tasks_t;

      static PostUI* create_window (GtkWindow*, Data&, Queue&, GroupServer&, Profiles&,
                                    GMimeMessage*, Prefs&, GroupPrefs&, EncodeCache&);

      void prompt_user_for_queueable_files (GtkWindow * parent, const Prefs& prefs);
      std::string prompt_user_for_upload_nzb_dir (GtkWindow * parent, const Prefs& prefs);

    protected:
      PostUI (GtkWindow*, Data&, Queue&, GroupServer&, Profiles&,
              GMimeMessage*, Prefs&, GroupPrefs&, EncodeCache&);
    public:
      ~PostUI ();

    public:
      GtkWidget * root() { return _root; }
      GtkWidget * part_select() { return _part_select; }
      GtkWidget * parts_store() { return _parts_store; }

      void rot13_selection ();
      void wrap_selection ();
      void wrap_body ();
      void spawn_editor ();
      void manage_profiles ();
      void set_charset (const StringView&);
      void apply_profile ();
      void save_draft ();
      void open_draft ();
      void import_draft (const char* fn);
      void prompt_for_charset ();
      void prompt_for_cte ();
      void send_now ();
      void send_and_save_now ();
      void add_files ();
      void close_window (bool flag=false);
      void set_wrap_mode (bool wrap);
      void set_always_run_editor (bool);

      void update_parts_tab();

      //popup action entries
      void remove_files  ();
      void clear_list    ();
      void select_parts  ();
      void move_up       ();
      void move_down     ();
      void move_top      ();
      void move_bottom   ();

      static void do_popup_menu (GtkWidget*, GdkEventButton *event, gpointer pane_g);
      static gboolean on_button_pressed (GtkWidget * treeview, GdkEventButton *event, gpointer userdata);
      static gboolean on_selection_changed  (GtkTreeSelection *s,gpointer p);

    private:
      void done_sending_message (GMimeMessage*, bool);
      void maybe_mail_message (GMimeMessage*);
      bool maybe_post_message (GMimeMessage*);
      enum Mode { DRAFTING, POSTING, UPLOADING, VIRTUAL};
      bool save_message_in_local_folder(const Mode& mode, const std::string& folder);
      std::string generate_message_id (const Profile& p);

    private:
      void update_widgetry ();
      void update_profile_combobox ();
      void populate_from_message (GMimeMessage*);
      void set_message (GMimeMessage*);

    private:
      virtual void on_progress_finished (Progress&, int status=OK);
      void on_progress_error (Progress&, const StringView&) override {}

      void on_progress_step (Progress&, int p) override {}
      void on_progress_status (Progress&, const StringView& s) override {}

    private:
      Data& _data;
      Queue& _queue;
      GroupServer& _gs;
      Profiles& _profiles;
      Prefs& _prefs;
      GroupPrefs& _group_prefs;
      GtkWidget * _root;
      GtkWidget * _part_select;
      GtkWidget * _from_combo;
      GtkWidget * _subject_entry;
      GtkWidget * _groups_entry;

      GtkWidget * _filequeue_store;
      GtkWidget * _parts_store;

      GtkWidget * _to_entry;
      GtkWidget * _followupto_entry;
      GtkWidget * _replyto_entry;
      GtkWidget * _body_view;
      GtkWidget * _user_agent_check;
      GtkWidget * _message_id_check;
      GtkTextBuffer * _body_buf;
      GtkTextBuffer * _headers_buf;
      GMimeMessage * _message;
      std::string _charset;
      TextMassager _tm;
      GtkUIManager * _uim;
      std::string _current_signature;
      TaskPost * _post_task;
      typedef std::map<std::string, std::string> str2str_t;
      str2str_t _hidden_headers;
      str2str_t _profile_headers;
      std::string _unchanged_body;
      int _wrap_pixels;
      std::string _spellcheck_language;

      GMimeContentEncoding _enc;

      /* binpost */
      bool _file_queue_empty;
      TaskUpload* _upload_ptr;
      int _total_parts;
      std::string _save_file;
      int _uploads;

    public:
      bool _realized;

    private:
      friend class UploadQueue;
      void on_queue_tasks_added (UploadQueue&, int index, int count) override;
      void on_queue_task_removed (UploadQueue&, Task&, int index) override;
      void on_queue_task_moved (UploadQueue&, Task&, int new_index, int old_index) override;

    private:
      /* GMIME: X-Face interval between spaces for proper folding. */
      enum { GMIME_FOLD_INTERVAL = 72, GMIME_FOLD_BASE64_INTERVAL = 64 };

      void add_actions (GtkWidget* box);
      void apply_profile_to_body ();
      void apply_profile_to_headers ();
      GMimeMessage * new_message_from_ui (Mode mode, bool copy_body=true);
      bool check_message (const Quark& server, GMimeMessage*, bool binpost=false);
      bool check_charset ();

    public:
      Profile get_current_profile ();
      GtkActionGroup * _agroup;

    private:
      GtkWidget* create_main_tab ();
      GtkWidget* create_extras_tab ();
      GtkWidget* create_filequeue_tab ();

      GtkWidget* create_filequeue_status_bar ();
      GtkWidget * _filequeue_eventbox;
      GtkWidget * _filequeue_label;
      void update_filequeue_label (GtkTreeSelection *selection=nullptr);

      GtkWidget* create_parts_tab ();

    private:
      std::string utf8ize (const StringView&) const;
      std::string get_body () const;
      int count_lines();
      gulong body_view_realized_handler;
      static void body_view_realized_cb (GtkWidget*, gpointer);
      GtkWidget* create_body_widget (GtkTextBuffer*&, GtkWidget*&, const pan::Prefs&);
      static void body_widget_resized_cb (GtkWidget*, GtkAllocation*, gpointer);

    private:

      unsigned long _body_changed_id;
      unsigned int _body_changed_idle_tag;
      static gboolean body_changed_idle (gpointer);
      static void body_changed_cb (GtkEditable*, gpointer);

      unsigned int _group_entry_changed_idle_tag;
      unsigned long _group_entry_changed_id;
      static gboolean group_entry_changed_idle (gpointer);
      static void group_entry_changed_cb (GtkEditable*, gpointer);

    protected:
      EncodeCache& _cache;

    public:
      void set_spellcheck_enabled (bool);

    public:
      tasks_t  get_selected_files () const;

    private:
      static void get_selected_files_foreach (GtkTreeModel*,
                      GtkTreePath*, GtkTreeIter*, gpointer);

      static void up_clicked_cb      (GtkButton*, PostUI*);
      static void down_clicked_cb    (GtkButton*, PostUI*);
      static void top_clicked_cb     (GtkButton*, PostUI*);
      static void bottom_clicked_cb  (GtkButton*, PostUI*);
      static void delete_clicked_cb  (GtkButton*, PostUI*);
      static void on_parts_box_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data);

    private:
      GtkAction * _spawner_action;
      std::unique_ptr<EditorSpawner> _spawner;
      void spawn_editor_dead(int, char *);
      TaskUpload* upload_ptr() { return _upload_ptr; }
      UploadQueue _upload_queue;
      Mutex mut;
      int _running_uploads;
      std::ofstream _out;
      static void message_id_toggled_cb (GtkToggleButton * tb, gpointer prefs_gpointer);

      int get_total_parts(const char* file);

    private:
      guint _draft_autosave_id;
      guint _draft_autosave_timeout;
      guint _draft_autosave_idle_tag;
      static gboolean draft_save_cb(gpointer ptr);

      /* override of "delete" */
      enum pages
      {
        PAGE_MAIN = 0,
        PAGE_EXTRAS = 1,
        PAGE_QUEUE = 2
      };
      GtkWidget * _notebook;
      int delete_override;
      static gboolean on_keyboard_key_pressed_cb (GtkWidget *, GdkEventKey *, gpointer);

    public:
      void set_draft_autosave_timeout(guint seconds)
        { _draft_autosave_timeout = seconds;}
  };
}

#endif
