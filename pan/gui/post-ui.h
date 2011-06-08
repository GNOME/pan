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

#ifndef __PostUI_h__
#define __PostUI_h__

#include <gmime/gmime-message.h>
#include <pan/gui/prefs.h>
#include <pan/general/progress.h>
#include <pan/tasks/queue.h>
#include <pan/usenet-utils/text-massager.h>
#include <pan/data/encode-cache.h>
#include "group-prefs.h"

namespace pan
{
  class Profiles;
  class TaskPost;
  class FileQueue;
  class Queue;


  typedef std::list<TaskUpload*> tasks_v;

  /**
   * Dialog for posting new messages Pan's GTK GUI.
   * @ingroup GUI
   */
  class PostUI: private Progress::Listener
  {
    public:
      static PostUI* create_window (GtkWindow*, Data&, Queue&, GroupServer&, Profiles&,
                                    GMimeMessage*, Prefs&, GroupPrefs&, EncodeCache&);

      void prompt_user_for_queueable_files (tasks_v& queue, GtkWindow * parent, const Prefs& prefs);
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

      void check_file_save(bool val) { _check_file_save = val; }

      void rot13_selection ();
      void wrap_selection ();
      void wrap_body ();
      void spawn_editor ();
      void manage_profiles ();
      void set_charset (const StringView&);
      void apply_profile ();
      void save_draft ();
      void open_draft ();
      void prompt_for_charset ();
      void send_now ();
      void add_files ();
      void close_window (bool flag=false);
      void set_wrap_mode (bool wrap);
      void set_always_run_editor (bool);
      void update_filequeue_tab();
      void update_parts_tab();

      //popup action entries
      void remove_files ();
      void clear_list   ();
      void select_parts ();
      void move_up      ();
      void move_down    ();
      void move_top     ();
      void move_bottom  ();

      static void do_popup_menu (GtkWidget*, GdkEventButton *event, gpointer pane_g);
      static gboolean on_button_pressed (GtkWidget * treeview, GdkEventButton *event, gpointer userdata);

    private:
      void done_sending_message (GMimeMessage*, bool);
      void maybe_mail_message (GMimeMessage*);
      bool maybe_post_message (GMimeMessage*);

    private:
      void update_widgetry ();
      void update_profile_combobox ();
      void populate_from_message (GMimeMessage*);
      void set_message (GMimeMessage*);

    private:
      virtual void on_progress_finished (Progress&, int status);
      virtual void on_progress_error (Progress&, const StringView&);

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
      GtkWidget * _lines_spin;

      GtkWidget * _to_entry;
      GtkWidget * _followupto_entry;
      GtkWidget * _replyto_entry;
      GtkWidget * _body_view;
      GtkWidget * _user_agent_check;
      GtkWidget * _save_check;
      GtkWidget * _message_id_check;
      GtkTextBuffer * _body_buf;
      GtkTextBuffer * _headers_buf;
      GMimeMessage * _message;
      std::string _charset;
      TextMassager _tm;
      GtkUIManager * _uim;
      GtkActionGroup * _agroup, * _pgroup;
      std::string _current_signature;
      GtkWidget * _post_dialog;
      TaskPost * _post_task;
      TaskUpload* _upload_task;
      std::vector<Article*> _uploaded;
      typedef std::map<std::string, std::string> str2str_t;
      str2str_t _hidden_headers;
      str2str_t _profile_headers;
      std::string _unchanged_body;
      int _wrap_pixels;

      /* binpost */
      bool _file_queue_empty;
      tasks_v _file_queue_tasks;
      TaskUpload* _upload_ptr;
      int _total_parts;
      std::string _save_file;
      bool _check_file_save;

    private:
      void add_actions (GtkWidget* box);
      void apply_profile_to_body ();
      void apply_profile_to_headers ();
      enum Mode { DRAFTING, POSTING };
      GMimeMessage * new_message_from_ui (Mode mode);
      Profile get_current_profile ();
      bool check_message (const Quark& server, GMimeMessage*);
      bool check_charset ();

    private:
      GtkWidget* create_main_tab ();
      GtkWidget* create_extras_tab ();
      GtkWidget* create_filequeue_tab ();
      GtkWidget* create_parts_tab ();
      GtkWidget* create_log_tab ();

    private:
      std::string utf8ize (const StringView&) const;
      std::string get_body () const;
      gulong body_view_realized_handler;
      static void body_view_realized_cb (GtkWidget*, gpointer);
      GtkWidget* create_body_widget (GtkTextBuffer*&, GtkWidget*&, const pan::Prefs&);
      static void body_widget_resized_cb (GtkWidget*, GtkAllocation*, gpointer);

    private:
      unsigned long _group_entry_changed_id;
      unsigned int _group_entry_changed_idle_tag;
      static gboolean group_entry_changed_idle (gpointer);
      static void group_entry_changed_cb (GtkEditable*, gpointer);

    protected:
      EncodeCache& _cache;

    public:
      void set_spellcheck_enabled (bool);
      void spawn_editor_dead(char *);

    public:
      tasks_v  get_selected_files () const;

    private:
      static void get_selected_files_foreach (GtkTreeModel*,
                      GtkTreePath*, GtkTreeIter*, gpointer);

      int get_top() ;
      int get_bottom() ;

      static void up_clicked_cb      (GtkButton*, PostUI*);
      static void down_clicked_cb    (GtkButton*, PostUI*);
      static void top_clicked_cb     (GtkButton*, PostUI*);
      static void bottom_clicked_cb  (GtkButton*, PostUI*);
      static void delete_clicked_cb  (GtkButton*, PostUI*);
      static void on_parts_box_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data);
      static void queue_save_toggled_cb (GtkToggleButton * tb, gpointer gp UNUSED);

    public:
      TaskUpload* upload_ptr() { return _upload_ptr; }

  };
}

#endif
