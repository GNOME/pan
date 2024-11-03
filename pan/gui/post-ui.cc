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

#include "post-ui.h"

#include <config.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unistd.h>
#include <gmime/gmime.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
extern "C" {
  #include <sys/stat.h>
  #include <sys/time.h>
}
#ifdef HAVE_GSPELL
#include <gspell/gspell.h>
#endif
#include <pan/data/data.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/usenet-utils/gpg.h>
#include <pan/usenet-utils/message-check.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/data/data.h>
#include <pan/tasks/nzb.h>
#include <pan/gui/gui.h>
#include <pan/tasks/task-post.h>
#include "e-charset-dialog.h"
#include "e-cte-dialog.h"
#include "editor-spawner.h"
#include "pad.h"
#include "hig.h"
#include "post.ui.h"
#include "profiles-dialog.h"
#include "url.h"


#ifdef HAVE_GSPELL
#define DEFAULT_SPELLCHECK_FLAG true
#else
#define DEFAULT_SPELLCHECK_FLAG false
#endif

using namespace pan;

#define USER_AGENT_PREFS_KEY "add-user-agent-header-when-posting"
#define MESSAGE_ID_PREFS_KEY "add-message-id-header-when-posting"

namespace pan
{
#ifndef HAVE_CLOSE
  inline int close(int fd) {return _close(fd);}
#endif

  bool remember_charsets (true);
  bool master_reply (true);
  bool gpg_enc (false);
  bool gpg_sign (true);
  bool user_has_gpg (false);

  void on_remember_charset_toggled (GtkToggleAction * toggle, gpointer)
  {
    remember_charsets = gtk_toggle_action_get_active (toggle);
  }

  void on_mr_toggled (GtkToggleAction * toggle, gpointer)
  {
    master_reply = gtk_toggle_action_get_active (toggle);
  }

  void on_enc_toggled (GtkToggleAction * toggle, gpointer post_g)
  {

    PostUI* self = static_cast<PostUI*>(post_g);
    if (!self->_realized) return;

    gpg_enc = gtk_toggle_action_get_active (toggle);
  }

  void on_sign_toggled (GtkToggleAction * toggle, gpointer post_g)
  {

    PostUI* self = static_cast<PostUI*>(post_g);
    if (!self->_realized) return;

    gpg_sign = gtk_toggle_action_get_active (toggle);
  }

  void on_spellcheck_toggled (GtkToggleAction * toggle, gpointer post_g)
  {
    const bool enabled = gtk_toggle_action_get_active (toggle);
    static_cast<PostUI*>(post_g)->set_spellcheck_enabled (enabled);
  }
}

namespace
{
  int
  find_task_index (GtkListStore * list, Task * task)
  {
    GtkTreeIter iter;
    int index (0);

    if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL(list), &iter)) do
    {
      TaskUpload * test;
      gtk_tree_model_get (GTK_TREE_MODEL(list), &iter, 2, &test, -1);
      if (test == task)
        return index;
      ++index;
    }
    while (gtk_tree_model_iter_next (GTK_TREE_MODEL(list), &iter));

    // not found
    return -1;
  }

  bool is_file_exist(const char *fileName)
      {
       std::ifstream infile(fileName);
       return infile.good();
      }
}

/***
****
***/

void
PostUI:: update_filequeue_label (GtkTreeSelection *selection)
{
    tasks_t tasks(get_selected_files());

    if (tasks.empty()) {
      _upload_queue.get_all_tasks(tasks);
    }

    char str[512];
    long kb(0);
    foreach (PostUI::tasks_t, tasks, it)
    {
      TaskUpload * task(dynamic_cast<TaskUpload*>(*it));
      if (task) {
        kb += task->_bytes / 1024;
      }
    }
    g_snprintf(
      str,
      sizeof(str),
      _("Upload queue: %llu tasks, %ld KB (~ %.2f MB) total."),
      tasks.size(),
      kb,
      kb / 1024.0f);
    gtk_label_set_text (GTK_LABEL(_filequeue_label), str);
}

/***
****
***/

void
PostUI :: on_queue_tasks_added (UploadQueue& queue, int index, int count)
{

  _uploads += count;

  GtkListStore *store = GTK_LIST_STORE(
                      gtk_tree_view_get_model(GTK_TREE_VIEW(_filequeue_store)));

  GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(_filequeue_store));
  g_object_ref(model);
  gtk_tree_view_set_model(GTK_TREE_VIEW(_filequeue_store), nullptr);

  for (int i=0; i<count; ++i)
  {
    const int pos (index + i);

    TaskUpload * task (dynamic_cast<TaskUpload*>(_upload_queue[pos]));
    if (!task) continue;

    GtkTreeIter iter;
    gtk_list_store_insert (store, &iter, pos);

    float size = task->_bytes/1024.0f;
    char numbuf[256];
    g_snprintf(numbuf,sizeof(numbuf), "%.2f", size);

    gtk_list_store_set (store, &iter,
                      0, pos+1,
                      1, task->_subject.c_str(),
                      2, task,
                      3, numbuf,
                      -1);
  }
  gtk_tree_view_set_model(GTK_TREE_VIEW(_filequeue_store), model);
  g_object_unref(model);

  update_filequeue_label();
}

void
PostUI :: on_queue_task_removed (UploadQueue&, Task& task, int index)
{

  --_uploads;
  if (_uploads == 0) _file_queue_empty = true;

  GtkListStore *store = GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(_filequeue_store)));

  const int list_index (find_task_index (store, &task));
  assert (list_index == index);
  GtkTreeIter iter;
  gtk_tree_model_iter_nth_child (GTK_TREE_MODEL(store), &iter, nullptr, index);
  gtk_list_store_remove (store, &iter);

  update_filequeue_label();
}

void
PostUI :: on_queue_task_moved (UploadQueue&, Task&, int new_index, int old_index)
{
  GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(_filequeue_store));
  GtkListStore* store = GTK_LIST_STORE(model);
  const int count (gtk_tree_model_iter_n_children (model, nullptr));
  std::vector<int> v (count);
  for (int i=0; i<count; ++i) v[i] = i;
  if (new_index < old_index) {
    v.erase (v.begin()+old_index);
    v.insert (v.begin()+new_index, old_index);
  } else {
    v.erase (v.begin()+old_index);
    v.insert (v.begin()+new_index, old_index);
  }
  gtk_list_store_reorder (store, &v.front());
}

void
PostUI :: set_spellcheck_enabled (bool enabled)
{
  _prefs.set_flag ("spellcheck-enabled", enabled);

#ifdef HAVE_GSPELL
  GtkTextView * gtk_view = GTK_TEXT_VIEW(_body_view);
  GError * err (nullptr);

  gboolean spell_attach = TRUE;
  const GspellLanguage *language = gspell_language_lookup(_spellcheck_language.c_str());
  GspellChecker* spell = gspell_checker_new (language);
    
  GspellTextView *gspell_view = gspell_text_view_get_from_gtk_text_view (gtk_view);
  gspell_text_view_basic_setup (gspell_view);
  gspell_text_view_set_inline_spell_checking (gspell_view, enabled);
  gspell_text_view_set_enable_language_menu (gspell_view, enabled);
#endif // HAVE_GSPELL
}

int
PostUI :: count_lines()
{
  GtkTextBuffer * buf (_body_buf);
  GtkTextView * view (GTK_TEXT_VIEW(_body_view));
  GtkTextIter body_start, body_end, line_end;
  gtk_text_buffer_get_bounds (buf, &body_start, &body_end);
  int count(0);
  while ((gtk_text_view_forward_display_line (view, &line_end))) ++count;
  return count;
}

/***
****  WRAP CODE
***/

/**
 * get the current body.
 * since Pan posts WYSIWYG, pull from the view's lines
 * rather than just using text_buffer_get_text(start,end)
 */
std::string
PostUI :: get_body () const
{
  std::string body;
  GtkTextBuffer * buf (_body_buf);
  GtkTextView * view (GTK_TEXT_VIEW(_body_view));
  const bool wrap (_prefs.get_flag ("compose-wrap-enabled", false));

  // walk through all the complete lines...
  GtkTextIter body_start, body_end, line_start, line_end;
  gtk_text_buffer_get_bounds (buf, &body_start, &body_end);
  line_start = line_end = body_start;
  while ((gtk_text_view_forward_display_line (view, &line_end))) {
    char * line = gtk_text_buffer_get_text (buf, &line_start, &line_end, false);
    body += line;
    g_free (line);
    if (wrap && *body.rbegin() != '\n')
      body += '\n';
    line_start = line_end;
  }

  // and maybe the last line doesn't have a linefeed yet...
  char * last_line = gtk_text_buffer_get_text (buf, &line_start, &body_end, false);
  if (last_line && *last_line)
    body += last_line;
  g_free (last_line);

  return body;
}

void
PostUI :: set_always_run_editor (bool run)
{
  _prefs.set_flag ("always-run-editor", run);
}

void
PostUI :: set_wrap_mode (bool wrap)
{
  _prefs.set_flag ("compose-wrap-enabled", wrap);

  if (_body_buf) {
    const std::string s (get_body());
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(_body_view),
                                 wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
    if (!s.empty())
      gtk_text_buffer_set_text (_body_buf, s.c_str(), s.size());
  }
}

/***
****  Menu and Toolbar
***/

namespace
{
  GtkWidget* get_focus (gpointer p) { return gtk_window_get_focus(GTK_WINDOW(static_cast<PostUI*>(p)->root())); }

  void do_cut      (GtkAction*, gpointer p) { g_signal_emit_by_name (get_focus(p), "cut_clipboard"); }
  void do_copy     (GtkAction*, gpointer p) { g_signal_emit_by_name (get_focus(p), "copy_clipboard"); }
  void do_paste    (GtkAction*, gpointer p) { g_signal_emit_by_name (get_focus(p), "paste_clipboard"); }
  void do_rot13    (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->rot13_selection(); }
  void do_save_upload (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->rot13_selection(); }
  void do_edit     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->spawn_editor (); }
  void do_profiles (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->manage_profiles (); }
  void do_send     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->send_now (); }
  void do_send_and_save (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->send_and_save_now (); }
  void do_save     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->save_draft (); }
  void do_open     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->open_draft (); }
  void do_charset  (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->prompt_for_charset (); }
  void do_cte      (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->prompt_for_cte (); }
  void do_close    (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->close_window (false); }
  void do_wrap     (GtkToggleAction * w, gpointer p) { static_cast<PostUI*>(p)->set_wrap_mode (gtk_toggle_action_get_active (w)); }
  void do_edit2    (GtkToggleAction * w, gpointer p) { static_cast<PostUI*>(p)->set_always_run_editor (gtk_toggle_action_get_active (w)); }
  void do_wrap_selected(GtkAction*, gpointer p) { static_cast<PostUI*>(p)->wrap_selection(); }
  void do_add_files          (GtkAction*, gpointer p) {static_cast<PostUI*>(p)->add_files(); }

  GtkActionEntry entries[] =
  {
    { "file-menu", nullptr, N_("_File"), nullptr, nullptr, nullptr },
    { "edit-menu", nullptr, N_("_Edit"), nullptr, nullptr, nullptr },
    { "profile-menu", nullptr, N_("_Profile"), nullptr, nullptr, nullptr },
    { "editors-menu", nullptr, N_("Set Editor"), nullptr, nullptr, nullptr },
    { "post-toolbar", nullptr, "post", nullptr, nullptr, nullptr },
    { "post-article", GTK_STOCK_EXECUTE, N_("_Send Article"), "<control>Return", N_("Send Article Now"), G_CALLBACK(do_send) },
    { "post-and-save-articles", GTK_STOCK_FLOPPY, N_("_Send and Save Articles to NZB"), nullptr, N_("Send and Save Articles to NZB"), G_CALLBACK(do_send_and_save) },
    { "set-charset", nullptr, N_("Set Character _Encoding..."), nullptr, nullptr, G_CALLBACK(do_charset) },
    { "set-encoding", nullptr, N_("Set Content _Transfer Encoding..."), nullptr, nullptr, G_CALLBACK(do_cte) },
    { "save-draft", GTK_STOCK_SAVE, N_("Sa_ve Draft"), "<control>s", N_("Save as a Draft for Future Posting"), G_CALLBACK(do_save) },
    { "open-draft", GTK_STOCK_OPEN, N_("_Open Draft..."), "<control>o", N_("Open an Article Draft"), G_CALLBACK(do_open) },
    { "close", GTK_STOCK_CLOSE, nullptr, nullptr, nullptr, G_CALLBACK(do_close) },
    { "cut", GTK_STOCK_CUT, nullptr, nullptr, nullptr, G_CALLBACK(do_cut) },
    { "copy", GTK_STOCK_COPY, nullptr, nullptr, nullptr, G_CALLBACK(do_copy) },
    { "paste", GTK_STOCK_PASTE, nullptr, nullptr, nullptr, G_CALLBACK(do_paste) },
    { "rot13", GTK_STOCK_REFRESH, N_("_Rot13"), "<control>r", N_("Rot13 Selected Text"), G_CALLBACK(do_rot13) },
    { "run-editor", GTK_STOCK_JUMP_TO, N_("Run _Editor"), "<control>e", N_("Run Editor"), G_CALLBACK(do_edit) },
    { "manage-profiles", GTK_STOCK_EDIT, N_("Edit P_osting Profiles"), nullptr, nullptr, G_CALLBACK(do_profiles) },
    { "add-files", GTK_STOCK_ADD, N_("Add _Files to Queue"), "<control>O", N_("Add Files to Queue"), G_CALLBACK(do_add_files) },
  };

  void do_remove_files       (GtkAction*, gpointer p) {static_cast<PostUI*>(p)->remove_files(); }
  void do_clear_list         (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->clear_list(); }
  void do_select_parts       (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->select_parts(); }
  void do_move_up            (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->move_up(); }
  void do_move_down          (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->move_down(); }
  void do_move_top           (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->move_top(); }
  void do_move_bottom        (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->move_bottom(); }

  GtkActionEntry filequeue_popup_entries[] =
  {

    { "remove-files", "Delete",
      N_("Remove from Queue"), nullptr,
      N_("Remove from Queue"),
      G_CALLBACK(do_remove_files) },

    { "clear-list", nullptr,
      N_("Clear List"), "",
      N_("Clear List"),
      G_CALLBACK(do_clear_list) },

    { "select-parts", nullptr,
      N_("Select Needed Parts"), "",
      N_("Select Needed Parts"),
      G_CALLBACK(do_select_parts) },

    { "move-up", nullptr,
      N_("Move Up"), "",
      N_("Move Up"),
      G_CALLBACK(do_move_up) },

    { "move-down", nullptr,
      N_("Move Down"), "",
      N_("Move Down"),
      G_CALLBACK(do_move_down) },

    { "move-top", nullptr,
      N_("Move to Top"), "",
      N_("Move to Top"),
      G_CALLBACK(do_move_top) },

    { "move-bottom", nullptr,
      N_("Move to Bottom"), "",
      N_("Move to Bottom"),
      G_CALLBACK(do_move_bottom) }

  };

  const GtkToggleActionEntry toggle_entries[] =
  {
    { "wrap", GTK_STOCK_JUSTIFY_FILL, N_("_Wrap Text"), nullptr, N_("Wrap Text"), G_CALLBACK(do_wrap), true },
    { "always-run-editor", nullptr, N_("Always Run Editor"), nullptr, nullptr, G_CALLBACK(do_edit2), false },
    { "remember-charset", nullptr, N_("Remember Character Encoding for This Group"), nullptr, nullptr, G_CALLBACK(on_remember_charset_toggled), true },
    { "master-reply", nullptr, N_("Thread Attached Replies"), nullptr, nullptr, G_CALLBACK(on_mr_toggled), true },
    { "gpg-encrypt", nullptr, N_("PGP-Encrypt the Article"), nullptr, nullptr, G_CALLBACK(on_enc_toggled), false },
    { "gpg-sign", nullptr, N_("PGP-Sign the Article"), nullptr, nullptr, G_CALLBACK(on_sign_toggled), false },
    { "spellcheck", nullptr, N_("Check _Spelling"), nullptr, nullptr, G_CALLBACK(on_spellcheck_toggled), true }
  };

  void add_widget (GtkUIManager*, GtkWidget* widget, gpointer vbox)
  {
    gtk_box_pack_start (GTK_BOX(vbox), widget, false, false, 0);
  }
}

#undef DEFAULT_CHARSET
#define DEFAULT_CHARSET  "UTF-8"

void
PostUI :: prompt_for_charset ()
{
  if (_charset.empty())
      _charset = DEFAULT_CHARSET;

  char * tmp = e_charset_dialog (_("Character Encoding"),
                                 _("New Article's Encoding:"),
                                 _charset.c_str(),
                                 GTK_WINDOW(root()));
  set_charset (tmp);
  free (tmp);
}

void
PostUI :: prompt_for_cte ()
{
  GMimeContentEncoding enc = e_cte_dialog (_("Content Transfer Encoding"),
                                 _("New Article's Content Transfer Encoding:"),
                                 _enc, GTK_WINDOW(root()));
  _enc = enc;
}

void
PostUI::  do_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  PostUI * self (static_cast<PostUI*>(userdata));
  GtkWidget * menu (gtk_ui_manager_get_widget (self->_uim, "/filequeue-popup"));
  gtk_menu_popup (GTK_MENU(menu), nullptr, nullptr, nullptr, nullptr,
                  (event ? event->button : 0),
                  (event ? event->time : 0));
}

namespace
{
  gboolean on_popup_menu (GtkWidget * treeview, gpointer userdata)
  {
    PostUI::do_popup_menu (treeview, nullptr, userdata);
    return true;
  }
}

gboolean
PostUI :: on_selection_changed  (GtkTreeSelection *s,gpointer p)
{
  static_cast<PostUI*>(p)->update_filequeue_label();
  return false;
}

gboolean
PostUI :: on_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{

  if (event->type == GDK_BUTTON_PRESS )
  {
    GtkTreeSelection *selection;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

    if ( event->button == 3)
    {

      if (gtk_tree_selection_count_selected_rows(selection)  <= 1)
      {
         GtkTreePath *path;
         /* Get tree path for row that was clicked */
         if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
                                           (gint) event->x,
                                           (gint) event->y,
                                           &path, nullptr, nullptr, nullptr))
         {
           gtk_tree_selection_unselect_all(selection);
           gtk_tree_selection_select_path(selection, path);
           gtk_tree_path_free(path);
         }
      }
      do_popup_menu(treeview, event, userdata);
      return true;
    }

  }
  return false;
}

void
PostUI :: add_actions (GtkWidget * box)
{
  _uim = gtk_ui_manager_new ();

  // read the file...
  char * filename = g_build_filename (file::get_pan_home().c_str(), "post.ui", nullptr);
  GError * err (nullptr);
  if (!gtk_ui_manager_add_ui_from_file (_uim, filename, &err)) {
    g_clear_error (&err);
    gtk_ui_manager_add_ui_from_string (_uim, fallback_post_ui, -1, &err);
  }
  if (err) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), filename, err->message);
    g_clear_error (&err);

  }
  g_free (filename);

  g_signal_connect (_uim, "add_widget", G_CALLBACK(add_widget), box);

  // add the actions...
  _agroup = gtk_action_group_new ("post");
  gtk_action_group_set_translation_domain (_agroup, nullptr);
  gtk_action_group_add_actions (_agroup, entries, G_N_ELEMENTS(entries), this);
  gtk_action_group_add_toggle_actions (_agroup, toggle_entries, G_N_ELEMENTS(toggle_entries), this);
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (_agroup, "always-run-editor")),
                                _prefs.get_flag ("always-run-editor", false));
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (_agroup, "spellcheck")),
                                _prefs.get_flag ("spellcheck-enabled", DEFAULT_SPELLCHECK_FLAG));
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (_agroup, "wrap")),
                                _prefs.get_flag ("compose-wrap-enabled", true));

   //add popup actions
  gtk_action_group_add_actions (_agroup, filequeue_popup_entries, G_N_ELEMENTS(filequeue_popup_entries), this);
  gtk_ui_manager_insert_action_group (_uim, _agroup, 0);

  gtk_action_group_set_sensitive(_agroup, true);

  //Remember the spawn button
  _spawner_action = gtk_action_group_get_action(_agroup, "run-editor");


}

void
PostUI :: set_charset (const StringView& charset)
{
  _charset = charset;
}

void
PostUI :: manage_profiles ()
{
  ProfilesDialog d (_data, _profiles, GTK_WINDOW(_root));
  gtk_dialog_run (GTK_DIALOG(d.root()));
  gtk_widget_destroy (d.root());
  update_profile_combobox ();
  apply_profile ();
}

void
PostUI :: rot13_selection ()
{
  GtkTextIter start, end;
  if (gtk_text_buffer_get_selection_bounds (_body_buf, &start, &end))
  {
    char * str (gtk_text_buffer_get_text (_body_buf, &start, &end, false));
    _tm.rot13_inplace (str);
    gtk_text_buffer_delete (_body_buf, &start, &end);
    gtk_text_buffer_insert (_body_buf, &start, str, strlen(str));
    g_free (str);
  }
}

void
PostUI :: wrap_selection ()
{
  GtkTextIter start, end;
  if (gtk_text_buffer_get_selection_bounds (_body_buf, &start, &end))
  {
    char * str (gtk_text_buffer_get_text (_body_buf, &start, &end, false));
    std::string s(str);
    s = _tm.fill(s);
    gtk_text_buffer_delete (_body_buf, &start, &end);
    gtk_text_buffer_insert (_body_buf, &start, s.c_str(), s.length() );
    g_free (str);
  }
}

namespace
{
  gboolean delete_event_cb (GtkWidget*, GdkEvent*, gpointer user_data)
  {
    static_cast<PostUI*>(user_data)->close_window (false);
    return true; // don't invoke the default handler that destroys the widget
  }

  gboolean delete_parts_cb (GtkWidget* w, GdkEvent*, gpointer user_data)
  {
    return false;
  }

}

void
PostUI :: close_window (bool flag)
{
  bool destroy_flag (flag);

  if (get_body() == _unchanged_body)
    destroy_flag = true;

  if (!destroy_flag) {
    GtkWidget * d = gtk_message_dialog_new (
      GTK_WINDOW(_root),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, nullptr);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
      _("Your changes will be lost!"),
      _("Close this window and lose your changes?"));
    gtk_dialog_add_buttons (GTK_DIALOG(d),
                            GTK_STOCK_GO_BACK, GTK_RESPONSE_NO,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                            nullptr);
    gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_NO);
    destroy_flag = gtk_dialog_run (GTK_DIALOG(d)) == GTK_RESPONSE_CLOSE;
    gtk_widget_destroy (d);
  }

  int w(450),h(450);
  gtk_window_get_size( GTK_WINDOW(_root), &w, &h);
  _prefs.set_int("post-ui-width", w);
  _prefs.set_int("post-ui-height", h);

  if (destroy_flag)
    gtk_widget_destroy (_root);
}

bool
PostUI :: check_message (const Quark& server, GMimeMessage * msg, bool binpost)
{

  MessageCheck :: unique_strings_t errors;
  MessageCheck :: Goodness goodness;

  quarks_t groups_this_server_has;
  _gs.server_get_groups (server, groups_this_server_has);
  /* skip some checks if the file queue is not empty or if the binpost flag is set, i.e. we want to upload binary data */
  MessageCheck :: message_check (msg, _hidden_headers["X-Draft-Attribution"],
                                 groups_this_server_has, errors, goodness, !_file_queue_empty || binpost);

  if (goodness.is_ok())
    return true;

  std::string s;
  foreach_const (MessageCheck::unique_strings_t, errors, it)
    s += *it + "\n";
  s.resize (s.size()-1); // eat trailing linefeed

  const GtkMessageType type (goodness.is_refuse() ? GTK_MESSAGE_ERROR : GTK_MESSAGE_WARNING);
  GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          type, GTK_BUTTONS_NONE, nullptr);
  HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
                           _("There were problems with this post."),
                           s.c_str());
  gtk_dialog_add_button (GTK_DIALOG(d), _("Go Back"), GTK_RESPONSE_CANCEL);
  if (goodness.is_warn())
    gtk_dialog_add_button (GTK_DIALOG(d), _("Continue Anyway"), GTK_RESPONSE_APPLY);
  const int response = gtk_dialog_run (GTK_DIALOG(d));
  gtk_widget_destroy (d);
  return response == GTK_RESPONSE_APPLY;
}

bool
PostUI :: check_charset ()
{
  const std::string charset (!_charset.empty() ? _charset : "UTF-8");
  // GtkTextBuffer is in UTF-8, so posting in UTF-8 is ok
  if (charset == "UTF-8")
    return true;

  // Check if body can be posted in the selected charset
  const std::string body (get_body ());
  char *tmp = g_convert (body.c_str(), -1, charset.c_str(), "UTF-8", nullptr, nullptr, nullptr);
  if (tmp) {
    g_free(tmp);
    return true;
  }

  // Wrong charset. Let GMime guess the best charset.
  const char * best_charset = g_mime_charset_best (body.c_str(), strlen (body.c_str()));
  if (best_charset == nullptr) best_charset = "ISO-8859-1";
  // GMime reports (some) charsets in lower case. Pan always uses uppercase.
  tmp = g_ascii_strup (best_charset, -1);

  // Prompt the user
  char * msg = g_strdup_printf (_("Message uses characters not specified in charset '%s' - possibly use '%s' "), charset.c_str(), tmp);
  GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE,
										  nullptr);
  HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
                           _("There were problems with this post."),
                           msg);
  gtk_dialog_add_button (GTK_DIALOG(d), _("Go Back"), GTK_RESPONSE_CANCEL);
  gtk_dialog_run (GTK_DIALOG(d));
  gtk_widget_destroy (d);
  g_free (tmp);
  g_free (msg);

  return false;
}


namespace
{
  GtkWidget * new_go_online_button ()
  {
    GtkWidget * button = gtk_button_new ();
    GtkWidget * label = gtk_label_new_with_mnemonic (_("Go _Online"));
    GtkWidget * image = gtk_image_new_from_stock (GTK_STOCK_NETWORK, GTK_ICON_SIZE_BUTTON);
    GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget * align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
    gtk_box_pack_start (GTK_BOX (hbox), image, false, false, 0);
    gtk_box_pack_end (GTK_BOX (hbox), label, false, false, 0);
    gtk_container_add (GTK_CONTAINER (align), hbox);
    gtk_container_add (GTK_CONTAINER (button), align);
    gtk_widget_show_all (button);
    return button;
  }
}

void
PostUI :: add_files ()
{
  if (!check_charset())
    return;
  prompt_user_for_queueable_files (GTK_WINDOW (gtk_widget_get_toplevel(_root)), _prefs);
}

void
PostUI :: send_now ()
{


  if (!check_charset())
    return;
  GMimeMessage * message (new_message_from_ui (POSTING));
  if (!maybe_post_message (message))
    g_object_unref (G_OBJECT(message));
}

void
PostUI :: send_and_save_now ()
{

  tasks_t tasks;
  _upload_queue.get_all_tasks(tasks);

  if (!check_charset())
    return;

  if (_file_queue_empty)
  {
    GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE, nullptr);
      HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
      _("The file queue is empty, so no files can be saved."),"");
      gtk_dialog_add_button (GTK_DIALOG(d), _("Go Back"), GTK_RESPONSE_CANCEL);
      gtk_dialog_run(GTK_DIALOG(d));
      gtk_widget_destroy(d);
      return;
  } else
    _save_file = prompt_user_for_upload_nzb_dir (GTK_WINDOW(_root), _prefs);

  // no file chosen, abort
  if (_save_file.empty()) return;

  // else, start upload (and update tasks' data)
  GMimeMessage * message (new_message_from_ui (UPLOADING));
  if (!maybe_post_message (message))
    g_object_unref (G_OBJECT(message));
}

void
PostUI :: done_sending_message (GMimeMessage * message, bool ok)
{
  if (ok) {
    _unchanged_body = get_body ();
    close_window (true);
  }

  g_object_unref (G_OBJECT(message));
}

namespace
{
  // http://www.rfc-editor.org/rfc/rfc1738.txt
  std::string encode (const StringView& in)
  {
    static std::set<char> keep;
    if (keep.empty()) {
      for (int i='0'; i<='9'; ++i) keep.insert(i); // always okay
      for (int i='a'; i<='z'; ++i) keep.insert(i); // always okay
      for (int i='A'; i<='Z'; ++i) keep.insert(i); // always okay
      for (const char *pch="$-_.+!*'(),"; *pch; ++pch) keep.insert(*pch); // okay in the right context
      for (const char *pch="$&+,/:;=?@"; *pch; ++pch) keep.erase(*pch); // reserved
      for (const char *pch="()"; *pch; ++pch) keep.erase(*pch); // gives thunderbird problems?
    }

    std::string out;
    foreach_const (StringView, in, pch) {
      if (keep.count(*pch))
        out += *pch;
      else {
        unsigned char uc = (unsigned char)*pch;
        char buf[8];
        g_snprintf (buf, sizeof(buf), "%%%02x", (int)uc);
        out += buf;
      }
    }
    return out;
  }
}

void
PostUI :: maybe_mail_message (GMimeMessage * message)
{
  std::string url, to, groups;
  gboolean unused;
  char * headers (g_mime_object_get_headers ((GMimeObject *) message, nullptr));
  char * body (pan_g_mime_message_get_body (message, &unused));
  StringView key, val, v(headers);
  v.trim ();
  while (v.pop_token (val, '\n') && val.pop_token(key,':')) {
    key.trim ();
    val.eat_chars (1);
    val.trim ();
    std::string key_enc (encode (key));
    std::string val_enc (encode (val));
    if (key == "To")
      to = val_enc;
    else if (key == "Newsgroups")
      groups = val;
    else if (key!="User-Agent" && key!="Mime-Version" && key!="Content-Type") {
      url += '&';
      url += key_enc;
      url += '=';
      url += val_enc;
    }
  }

  if (!groups.empty()) {
    char * pch = g_strdup_printf ("[This mail was also posted to %s.]\n\n%s", groups.c_str(), body);
    g_free (body);
    body = pch;
  }

  url += std::string("&body=") + encode(body);
  url[0] = '?';
  url = std::string("mailto:") + to + url;

  if (!to.empty())
    URL :: open (_prefs, url.c_str(), URL::MAIL);
  done_sending_message (message, true);

  g_free (body);
  g_free (headers);
}

void
PostUI :: on_progress_finished (Progress&, int status) // posting finished
{

  if (status==-1) { --_running_uploads; return; }

  if (_file_queue_empty)
  {
    _post_task->remove_listener (this);
    GMimeMessage * message (_post_task->get_message ());
    if (status != OK) // error posting.. stop.
      done_sending_message (message, false);
    else
      maybe_mail_message (message);
  } else
  {
    --_running_uploads;
    int no = status;
    TaskUpload * ptr = dynamic_cast<TaskUpload*>(_upload_queue[no]);

    if (!_save_file.empty())
    {
      mut.lock();
        if (ptr) NZB :: upload_list_to_xml_file (_out, ptr->_upload_list);
        if (_running_uploads==0 )
        {
          NZB::print_footer(_out);
          _out.close();
        }
      mut.unlock();
    }
    if (_running_uploads==0) close_window(true);
  }

}

std::string
PostUI::generate_message_id (const Profile& p)
{
  return !p.fqdn.empty()
      ? GNKSA::generate_message_id (p.fqdn)
      : GNKSA::generate_message_id_from_email_address (p.address);
}

bool
PostUI :: save_message_in_local_folder(const Mode& mode, const std::string& folder)
{
	  // the following message is constructed solely for the purpose of adding the current message to
	  // a local folder of pan
	  GMimeMessage* msg = new_message_from_ui(VIRTUAL);
	  Profile p(get_current_profile());

	  //domain name
	  std::string mid = generate_message_id(p);
	  std::string author;
	  p.get_from_header(author);
	  std::string subject(utf8ize (g_mime_message_get_subject (msg)));
	  const char * refs = g_mime_object_get_header(GMIME_OBJECT(msg), "References");
	  g_mime_object_set_header((GMimeObject *) msg, "Newsgroups", folder.c_str(), nullptr);

	  // pseudo mid to get data from cache
	  std::string message_id = pan_g_mime_message_set_message_id(msg, mid.c_str());
	  std::stringstream xref;
	  time_t posted = time(nullptr); // use posted as article number, this is unique anyway
	  xref << folder << ":"<<posted;
	  const Article* article = _data.xover_add (p.posting_server, folder, subject, author, posted, message_id, refs, sizeof(*msg), 3, xref.str(), true);
	  if (article)
	  {
          GDateTime * postedGDT = g_date_time_new_from_unix_utc(posted);
		  g_mime_message_set_date(msg, postedGDT);
		  ArticleCache& cache(_data.get_cache());
		  ArticleCache :: CacheResponse response = cache.add(mid, g_mime_object_to_string(GMIME_OBJECT(msg), nullptr), true);
		  g_object_unref(msg);

		  if (response.type != ArticleCache::CACHE_OK)
		  {
			  std::string reason(response.type == ArticleCache::CACHE_IO_ERR
				? _("IO Error") : _("No space left on device"));
			  Log::add_err_va(_("Error copying message to %s folder. Reason: %s"), folder.c_str(), reason.c_str());
			  return false;
		  }
	  }
	  else
	  {
		  Log::add_err_va(_("Error creating message in %s mail folder: Invalid article."), folder.c_str());
		  return false;
	  }
	  return true;
}

bool
PostUI :: maybe_post_message (GMimeMessage * message)
{
  /**
  ***  Find the server to use
  **/
  g_return_val_if_fail(message, false);

  // get the profile...
  const Profile p (get_current_profile ());
  // get the server associated with that profile...
  const Quark& server (p.posting_server);
  // if the server's invalid, bitch about it to the user
  std::string error_msg;
  bool error = false;
  if (server.empty() || !_data.get_servers().count(server))
    error_msg = _("No posting server is set for this posting profile.\nPlease edit the profile via Edit|Manage Posting Profiles.");

  //invalid connection count, can't post
  Data::Server* s = _data.find_server(server);
  if (s && s->max_connections == 0)
    error_msg =  _("The selected posting server is currently disabled. Please choose an appropriate alternative.");

  if (!error_msg.empty())
  {
    GtkWidget * d = gtk_message_dialog_new (
      GTK_WINDOW(_root),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, "%s", error_msg.c_str());
    gtk_dialog_run (GTK_DIALOG(d));
    gtk_widget_destroy (d);
    return false;
  }

  /**
  ***  Make sure the message is OK...
  **/
  if (!check_message (server, message, !_file_queue_empty))
    return false;

  /**
  *** If this is email only, skip the rest of the posting...
  *** we only stayed this long to get check_message()
  **/
  const StringView groups (g_mime_object_get_header ((GMimeObject *) message, "Newsgroups"));
  if (groups.empty() && _file_queue_empty) {
    maybe_mail_message (message);
    return true;
  }

  g_object_unref(message);

  /**
  ***  Make sure we're online...
  **/
  if (!_queue.is_online())
  {
    GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE, nullptr);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
                             _("Pan is Offline."),
                             _("Go online to post the article?"));
    gtk_dialog_add_button (GTK_DIALOG(d), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_action_widget (GTK_DIALOG(d), new_go_online_button(), GTK_RESPONSE_OK);
    const int response = gtk_dialog_run (GTK_DIALOG(d));
    gtk_widget_destroy (d);
    if (response !=  GTK_RESPONSE_OK)
      return false;
    _queue.set_online (true);
  }

  //TODO implement callback!!
  gtk_widget_hide (_root);

  save_message_in_local_folder(POSTING, "Sent");

  GMimeMessage* msg = new_message_from_ui(POSTING);

  if(_file_queue_empty)
  {
    bool go_on(true);
#ifdef HAVE_GMIME_CRYPTO
    /* adding yourself to the list of recipients */
    GPtrArray * rcp;

    if (user_has_gpg)
    {
      rcp = g_ptr_array_new();
      g_ptr_array_add(rcp, (gpointer)p.gpg_sig_uid.c_str());
    }

    if (user_has_gpg)
    {
      if (gpg_sign && !gpg_enc)
        go_on = go_on && message_add_signed_part(p.gpg_sig_uid, get_body(), msg);
      else if (user_has_gpg && gpg_enc && !gpg_sign)
        go_on = go_on && gpg_encrypt(p.gpg_sig_uid, get_body(), msg, rcp, false);
      else if (user_has_gpg && gpg_enc && gpg_sign)
        go_on = go_on && gpg_encrypt(p.gpg_sig_uid, get_body(), msg, rcp, true);
    }
#endif
    if (go_on)
    {
      _post_task = new TaskPost (server, msg);
      _post_task->add_listener (this);
      _queue.add_task (_post_task, Queue::TOP);
    }
    else
    {
      if (user_has_gpg)
         Log::add_err_va(_("Error signing/encrypting your message. Perhaps you misspelled your email address (%s)?"),p.gpg_sig_uid.c_str());
    }

  } else {

    // prepend header for xml file (if one was chosen)
    if (!_save_file.empty())
    {
      _out.open(_save_file.c_str(), std::fstream::out | std::fstream::app);
      NZB::print_header(_out);
    }

    std::vector<Task*> tasks;
    _upload_queue.get_all_tasks(tasks);
    _running_uploads = tasks.size();
    if (master_reply) ++_running_uploads;

    // generate domain name for upload if the flag is set / a save-file is set
    bool custom_mid(_prefs.get_flag(MESSAGE_ID_PREFS_KEY,false) || !_save_file.empty());

    std::string last_mid;
    std::string first_mid;

    Article a;
    TaskUpload * tmp (dynamic_cast<TaskUpload*>(tasks[0]));
    if (tmp) a = tmp->_article;

    if (master_reply)
    {

      // master article, other attachments are threaded as replies to this
      const Profile profile (get_current_profile ());
      first_mid = generate_message_id(p);

      TaskUpload::UploadInfo f;
      f.total=1;
      f.bpf = _prefs.get_int("upload-option-bpf",512*1024);
      TaskUpload::Needed n;
      n.mid = first_mid;

      {
        TaskUpload * tmp = new TaskUpload(a.subject.to_string(),profile.posting_server,_cache,a,f,msg);
        tmp->_needed.insert(std::pair<int, TaskUpload::Needed>(1,n));
        tmp->_queue_pos = -1;
        _queue.add_task (tmp, Queue::BOTTOM);
        tmp->add_listener(this);
      }
    }
    else
    {
      g_object_unref(G_OBJECT(msg));
    }


    /* init taskupload variables before adding the tasks to the queue for processing */
    char buf[2048];
    int cnt(0);

    foreach (PostUI::tasks_t, tasks, it)
    {

      TaskUpload * t (dynamic_cast<TaskUpload*>(*it));

      const char* basename = t->_basename.c_str();
      TaskUpload::Needed n;

      foreach (std::set<int>, t->_wanted, pit)
      {
        if (custom_mid)
        {
            n.mid = generate_message_id(p);
            if (first_mid.empty()) first_mid = n.mid;
        }

        g_snprintf(buf,sizeof(buf),"%s.%d", basename, *pit);
        n.message_id = buf;
        n.partno = *pit;
        n.last_mid = last_mid;
        t->_first_mid = first_mid;
        last_mid = n.mid;
        t->_needed.insert(std::pair<int,TaskUpload::Needed>(*pit,n));
      }
      t->build_needed_tasks();
      t->_save_file = _save_file;
      t->_queue_pos = cnt++;

      _queue.add_task (*it, Queue::BOTTOM);
      t->add_listener(this);
    }
  }

  /**
  ***  Maybe remember the charsets.
  **/
  if (remember_charsets) {
    const char * text = gtk_entry_get_text (GTK_ENTRY(_groups_entry));
    StringView line(text), groupname;
    while (line.pop_token (groupname, ',')) {
      groupname.trim ();
      if (!groupname.empty())
        _group_prefs.set_string (groupname, "character-encoding", _charset);
    }
  }

  return true;
}

/***
****
***/

namespace {
class Destroyer {
  public:
    Destroyer(char *f) :
      _fname(f)
    {
    }

    ~Destroyer()
    {
      g_free(_fname);
    }

    void retain()
    {
      _fname = nullptr;
    }

  private:
    char *_fname;
};
}

void
PostUI :: spawn_editor ()
{

  //Protect against bouncy keypresses
  if (not gtk_action_get_sensitive(_spawner_action)) {
    return;
  }

  // open a new tmp file
  char * fname (nullptr);

  {
    GError * err = nullptr;
    const int fd (g_file_open_tmp ("pan_edit_XXXXXX", &fname, &err));
    if (fd == -1) {
      Log::add_err (err && err->message ? err->message : _("Error opening temporary file"));
      g_clear_error (&err);
      return;
    }
    close(fd);
  }

  Destroyer d(fname);

  FILE *fp = fopen (fname, "w");
  if (fp == nullptr) {
    Log::add_err_va (_("Error creating temporary file: %s"), g_strerror(errno));
    return;
  }

  const std::string body (get_body ());

  if (fwrite (body.c_str(), sizeof(char), body.size(), fp) != body.size()) {
    fclose(fp);
    Log::add_err_va (_("Error writing article to temporary file: %s"), g_strerror(errno));
    return;
  }

  fclose(fp);

  try
  {
    using namespace std::placeholders;
    _spawner.reset(
      new EditorSpawner(fname,
                        std::bind(&PostUI::spawn_editor_dead, this, _1, _2),
                        _prefs));
    d.retain();
    gtk_action_set_sensitive(_spawner_action, false);
  }
  catch (EditorSpawnerError const &)
  {
    //Do nothing. There should be a big red exclamation on the status line
  }
}

void
PostUI::spawn_editor_dead(int status, char *fname)
{
  GtkTextBuffer * buf(_body_buf);

  if (buf)
  {

    // read the file contents back in
    std::string txt;
    if (file::get_text_file_contents(fname, txt))
    {
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds(buf, &start, &end);
      gtk_text_buffer_delete(buf, &start, &end);
      gtk_text_buffer_insert(buf, &start, txt.c_str(), txt.size());
    }

  }

  // cleanup
  ::remove(fname);
  g_free(fname);

  gtk_action_set_sensitive(_spawner_action, true);
  _spawner.reset();
  gtk_window_present(GTK_WINDOW(root()));
}

namespace
{
  std::string& get_draft_filename ()
  {
    static std::string fname;

    if (fname.empty())
    {
      fname = file::get_pan_home ();
      char * pch = g_build_filename (fname.c_str(), "article-drafts", nullptr);
      file :: ensure_dir_exists (pch);
      fname = pch;
      g_free (pch);
    }

    return fname;
  }
}

void
PostUI :: open_draft ()
{
  GtkWidget * d = gtk_file_chooser_dialog_new (_("Open Draft Article"),
                                               GTK_WINDOW(_root),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                               nullptr);

  std::string& draft_filename (get_draft_filename ());
  if (g_file_test (draft_filename.c_str(), G_FILE_TEST_IS_DIR))
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(d), draft_filename.c_str());
  else
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(d), draft_filename.c_str());

  if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
  {
    char * pch = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(d));
    draft_filename = pch;
    g_free (pch);

    std::string txt;
    if (file :: get_text_file_contents (draft_filename, txt))
    {
      GMimeStream * stream = g_mime_stream_mem_new_with_buffer (txt.c_str(), txt.size());
      GMimeParser * parser = g_mime_parser_new_with_stream (stream);
      GMimeMessage * message = g_mime_parser_construct_message (parser, nullptr);
      if (message) {
        set_message (message);
        g_object_unref (G_OBJECT(message));
      }
      g_object_unref (G_OBJECT(parser));
      g_object_unref (G_OBJECT(stream));
    }
  }
  gtk_widget_destroy (d);
}

void
PostUI :: import_draft (const char* fn)
{
//    const char * draft = fn;
//    std::string txt;
//    if (file :: get_text_file_contents (draft, txt))
//    {
//      GMimeStream * stream = g_mime_stream_mem_new_with_buffer (txt.c_str(), txt.size());
//      GMimeParser * parser = g_mime_parser_new_with_stream (stream);
//      GMimeMessage * message = g_mime_parser_construct_message (parser);
//      if (message) {
//        set_message (message);
//        g_object_unref (G_OBJECT(message));
//      }
//      g_object_unref (G_OBJECT(parser));
//      g_object_unref (G_OBJECT(stream));
//    }
}

namespace
{
  const char * get_user_agent ()
  {
#ifdef GIT_REV
    return "Pan/" PACKAGE_VERSION " (" VERSION_TITLE "; " GIT_REV ")";
#else
    return "Pan/" PACKAGE_VERSION " (" VERSION_TITLE ")";
#endif
  }

  bool header_has_dedicated_entry (const StringView& name)
  {
    return (name == "Subject")
        || (name == "Newsgroups")
        || (name == "To")
        || (name == "From")
        || (name == "Followup-To")
        || (name == "Reply-To");
  }

  bool extra_header_is_editable (const StringView& name,
                                 const StringView& value)
  {
    const bool keep = !name.empty() // make sure header exists
        && (!header_has_dedicated_entry (name)) // not an extra header
        && (name != "Xref") // not editable
        && (name != "Message-ID")
        && (name != "References")
        && ((name != "User-Agent") || (value != get_user_agent()))
        && (name.strstr ("Content-") != name.str)
        && (name.strstr ("X-Draft-") != name.str);
    return keep;
  }
}

GMimeMessage*
PostUI :: new_message_from_ui (Mode mode, bool copy_body)
{

  GMimeMessage * msg(nullptr);
  msg = g_mime_message_new (true);
  const char * charset_cstr = _charset.c_str();

  // headers from the ui: From
  const Profile profile (get_current_profile ());
  std::string s;
  profile.get_from_header (s);
  g_mime_message_add_mailbox (msg, GMIME_ADDRESS_TYPE_FROM, profile.username.c_str(), profile.address.c_str());

  // headers from the ui: Subject
  const char * cpch (gtk_entry_get_text (GTK_ENTRY(_subject_entry)));
  if (cpch) {
    g_mime_message_set_subject (msg, cpch, charset_cstr);
  }

  // headers from the ui: To
  const StringView to (gtk_entry_get_text (GTK_ENTRY(_to_entry)));
  if (!to.empty())
    pan_g_mime_message_add_recipients_from_string (msg, GMIME_ADDRESS_TYPE_TO, to.str);

  // headers from the ui: Newsgroups
  const StringView groups (gtk_entry_get_text (GTK_ENTRY(_groups_entry)));
  if (!groups.empty())
    g_mime_object_set_header ((GMimeObject *) msg, "Newsgroups", groups.str, charset_cstr);

  // headers from the ui: Followup-To
  const StringView followupto (gtk_entry_get_text (GTK_ENTRY(_followupto_entry)));
  if (!followupto.empty())
    g_mime_object_set_header ((GMimeObject *) msg, "Followup-To", followupto.str, charset_cstr);

  // headers from the ui: Reply-To
  const StringView replyto (gtk_entry_get_text (GTK_ENTRY(_replyto_entry)));
  if (!replyto.empty())
    g_mime_object_set_header ((GMimeObject *) msg, "Reply-To", replyto.str, charset_cstr);

  // headers from posting profile (via prefs): Face
  if (!profile.face.empty())
  {
    std::string f;
    f += profile.face;

    // GMIME_FOLD_BASE64_INTERVAL - 6: Accounting for 'Face: ' beginning of header
    for (int i = GMIME_FOLD_BASE64_INTERVAL - 6; i < f.length(); i += GMIME_FOLD_BASE64_INTERVAL)
    {
      f.insert (i, " ");
    }
    g_mime_object_set_header ((GMimeObject *) msg, "Face", f.c_str(), charset_cstr);
  }

  // headers from posting profile(via prefs): X-Face
  if (!profile.xface.empty())
  {
    std::string f;
    f += profile.xface;

    // GMIME_FOLD_INTERVAL - 8: Accounting for 'X-Face: ' beginning of header
    for(int i = GMIME_FOLD_INTERVAL - 8; i < f.length(); i += GMIME_FOLD_INTERVAL)
    {
      f.insert(i, " ");
    }
    g_mime_object_set_header ((GMimeObject *) msg, "X-Face", f.c_str(), charset_cstr);
  }

  // add the 'hidden headers' (references)
  const gchar * h_key_str;
  foreach_const (str2str_t, _hidden_headers, it)
    if ((mode==DRAFTING) || (it->first.find ("X-Draft-")!=0))
    {
      h_key_str = it->first.c_str();
      if ( g_ascii_strncasecmp (h_key_str, "Content", 7) )
      {
        g_mime_object_set_header ((GMimeObject *) msg, it->first.c_str(), it->second.c_str(), charset_cstr);
      }
    }

  // build headers from the 'more headers' entry field
  std::map<std::string,std::string> headers;
  GtkTextBuffer * buf (_headers_buf);
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  char * pch = gtk_text_buffer_get_text (buf, &start, &end, false);
  StringView key, val, v(pch);
  v.trim ();
  while (v.pop_token (val, '\n') && val.pop_token(key,':')) {
    key.trim ();
    val.eat_chars (1);
    val.trim ();
    std::string key_str (key.to_string());
    if (extra_header_is_editable (key, val))
      g_mime_object_set_header ((GMimeObject *) msg, key.to_string().c_str(),
                                val.to_string().c_str(), charset_cstr);
  }
  g_free (pch);

  // User-Agent
  if ((mode==POSTING || mode == UPLOADING) && _prefs.get_flag (USER_AGENT_PREFS_KEY, true))
    g_mime_object_set_header ((GMimeObject *) msg, "User-Agent", get_user_agent(), charset_cstr);

  // Message-ID for single text-only posts
  if (mode==DRAFTING || ((mode==POSTING || mode==UPLOADING) && _prefs.get_flag (MESSAGE_ID_PREFS_KEY, false))) {
    const std::string message_id = generate_message_id(profile);
    pan_g_mime_message_set_message_id (msg, message_id.c_str());
  }

  // body & charset
  {
    std::string body;
    if (copy_body) body = get_body();

    GMimeStream *  stream =  g_mime_stream_mem_new_with_buffer (body.c_str(), body.size());

    const std::string charset ((mode==POSTING && !_charset.empty()) ? _charset : "UTF-8");
    if (charset != "UTF-8") {
      // add a wrapper to convert from UTF-8 to $charset
      GMimeStream * tmp = g_mime_stream_filter_new (stream);
      g_object_unref (stream);
      GMimeFilter * filter = g_mime_filter_charset_new ("UTF-8", charset.c_str());
      g_mime_stream_filter_add (GMIME_STREAM_FILTER(tmp), filter);
      g_object_unref (filter);
      stream = tmp;
    }
    GMimeDataWrapper * content_object = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_DEFAULT);
    g_object_unref (stream);
    GMimePart * part = g_mime_part_new ();
    g_mime_part_set_content (part, content_object);

    pch = g_strdup_printf ("text/plain; charset=%s", charset.c_str());
    GMimeContentType * type = g_mime_content_type_parse (nullptr, pch);
    g_free (pch);
    g_mime_object_set_content_type ((GMimeObject *) part, type); // part owns type now. type isn't refcounted.

    if (mode != UPLOADING) g_mime_part_set_content_encoding (part, _enc);

    g_object_unref (content_object);
    g_mime_message_set_mime_part (msg, GMIME_OBJECT(part));
    g_object_unref (part);
  }

  return msg;

}

void
PostUI :: save_draft ()
{
  GtkWidget * d = gtk_file_chooser_dialog_new (
    _("Save Draft Article"),
    GTK_WINDOW(_root),
    GTK_FILE_CHOOSER_ACTION_SAVE,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
    nullptr);

   std::string draft_filename;
   char* filename;
   GMimeMessage* msg;
   bool do_overwrite = false;
   bool need_overwrite = false;
   bool select_ok = false;

   gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);
   draft_filename = get_draft_filename ();

  if (g_file_test (draft_filename.c_str(), G_FILE_TEST_IS_DIR))
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(d), draft_filename.c_str());
  else
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(d), draft_filename.c_str());

  do {
      if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
        {
          msg = new_message_from_ui (UPLOADING);
          filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(d));
          draft_filename = filename;
          select_ok = true;

          if (is_file_exist(draft_filename.c_str()))
          {
              need_overwrite = true;
              GtkWidget * dialog_w = gtk_message_dialog_new (
               GTK_WINDOW(_root),
               GTK_DIALOG_DESTROY_WITH_PARENT,
               GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, nullptr);
              HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(dialog_w),
               _("File already exists."),
               _("Overwrite it?"));
              gtk_dialog_add_buttons (GTK_DIALOG(dialog_w),
                         GTK_STOCK_GO_BACK, GTK_RESPONSE_NO,
                         GTK_STOCK_YES, GTK_RESPONSE_OK,
                         nullptr);
              gtk_dialog_set_default_response (GTK_DIALOG(dialog_w), GTK_RESPONSE_NO);
              switch(gtk_dialog_run(GTK_DIALOG(dialog_w)))
              {
              case GTK_RESPONSE_OK:
                  do_overwrite = true;
                  break;
              case GTK_RESPONSE_NO:
                  do_overwrite = false;
                  break;
              default:
                  do_overwrite = false;
                  break;
              }
              gtk_widget_destroy (dialog_w);
          }
          else {
              need_overwrite = false;
          }
        } else
      {
          select_ok = false;
      }

     } while ( do_overwrite != need_overwrite );

   if (do_overwrite == need_overwrite && select_ok)
    {
      errno = 0;
      std::ofstream o (filename);
      char * pch = g_mime_object_to_string ((GMimeObject *) msg, nullptr);
      o << pch;
      o.close ();

      if (o.fail())
      {
          GtkWidget * e = gtk_message_dialog_new (
              GTK_WINDOW(d),
              GTK_DIALOG_DESTROY_WITH_PARENT,
              GTK_MESSAGE_ERROR,
              GTK_BUTTONS_CLOSE,
              _("Unable to save \"%s\" %s"), filename, file::pan_strerror(errno));
          gtk_dialog_run (GTK_DIALOG(e));
          gtk_widget_destroy (e);
      }

      g_free (pch);
      g_free (filename);
      g_object_unref (msg);
      save_message_in_local_folder(DRAFTING, "Drafts");
      _unchanged_body = get_body ();
    }

  gtk_widget_destroy (d);
}

void
PostUI :: body_widget_resized_cb (GtkWidget      * w,
                                  GtkAllocation  * allocation,
                                  gpointer         self)
{
  gtk_text_view_set_right_margin (
    GTK_TEXT_VIEW(w),
    allocation->width - static_cast<PostUI*>(self)->_wrap_pixels);
}

GtkWidget*
PostUI :: create_body_widget (GtkTextBuffer*& buf, GtkWidget*& view, const Prefs &prefs)
{
  const int WRAP_COLS = 75;
  const int VIEW_COLS = 80;

  view = gtk_text_view_new ();
  buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW(view));

  // always use a monospace font in the compose window
  const std::string str (prefs.get_string ("monospace-font", "Monospace 10"));
  PangoFontDescription *pfd (pango_font_description_from_string (str.c_str()));
  gtk_widget_override_font (view, pfd);

  // figure out how wide the text is before the wrap point
  PangoContext * context = gtk_widget_create_pango_context (view);
  pango_context_set_font_description (context, pfd);
  PangoLayout * layout = pango_layout_new (context);
  std::string s (WRAP_COLS, 'A');
  pango_layout_set_text (layout, s.c_str(), s.size());
  PangoRectangle r;
  pango_layout_get_extents (layout, &r, nullptr);
  _wrap_pixels = PANGO_PIXELS(r.width);

  // figure out how wide we want to make the window
  s.assign (VIEW_COLS, 'A');
  pango_layout_set_text (layout, s.c_str(), s.size());
  pango_layout_get_extents (layout, &r, nullptr);
  gtk_widget_set_size_request (view, PANGO_PIXELS(r.width), -1 );

  // set the rest of the text view's policy
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
  gtk_text_view_set_editable (GTK_TEXT_VIEW(view), true);
  GtkWidget * scrolled_window = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scrolled_window),
                                       GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_window), view);
  g_signal_connect (view, "size-allocate", G_CALLBACK(body_widget_resized_cb), this);

  // cleanup
  g_object_unref (G_OBJECT(layout));
  g_object_unref (G_OBJECT(context));
  pango_font_description_free (pfd);

  return scrolled_window;
}

void
PostUI :: update_profile_combobox ()
{
  // get the old selection
  GtkComboBoxText * combo (GTK_COMBO_BOX_TEXT (_from_combo));
  char * active_text = gtk_combo_box_text_get_active_text (combo);

  // if there's not already a selection,
  // pull the default for the newsgroup
  if (!active_text)
  {
    const char * text = gtk_entry_get_text (GTK_ENTRY(_groups_entry));
    StringView line(text), groupname;
    while (line.pop_token (groupname, ',')) {
      groupname.trim ();
      if (!groupname.empty()) {
        std::string profile (_group_prefs.get_string (groupname, "posting-profile", ""));
        if (!profile.empty())
          active_text = g_strdup (profile.c_str());
      }
    }
  }

  // tear out the old entries
  GtkTreeModel * model (gtk_combo_box_get_model (GTK_COMBO_BOX(combo)));
  for (int i(0), qty(gtk_tree_model_iter_n_children(model,nullptr)); i<qty; ++i)
    gtk_combo_box_text_remove (combo, 0);

  // add the new entries
  typedef std::set<std::string> names_t;
  const names_t profile_names (_profiles.get_profile_names ());
  int index (0);
  int sel_index (0);
  foreach_const (names_t, profile_names, it) {
    gtk_combo_box_text_append_text (combo, it->c_str());
    if (active_text && (*it == active_text))
      sel_index = index;
    ++index;
  }

  // ensure _something_ is selected...
  gtk_combo_box_set_active (GTK_COMBO_BOX(combo), sel_index);

  // cleanup
  g_free (active_text);
}


namespace
{
  void
  load_signature (const StringView& sigfile, int type, std::string& setme)
  {
    setme.clear ();

    // check for an empty string
    StringView v (sigfile);
    v.trim ();
    if (v.empty())
      return;

    char * pch = g_strndup (v.str, v.len);
    std::string sig;

    if (type == Profile::TEXT)
    {
      sig = pch;
    }
    else if (type == Profile::FILE)
    {
      file :: get_text_file_contents (pch, sig);
    }
    else if (type == Profile::COMMAND)
    {
      int argc = 0;
      char ** argv = nullptr;

      if (g_file_test (pch, G_FILE_TEST_EXISTS))
      {
        argc = 1;
        argv = g_new (char*, 2);
        argv[0] = g_strdup (pch);
        argv[1] = nullptr; /* this is for g_strfreev() */
      }
      else // parse it...
      {
        GError * err = nullptr;
        if (!g_shell_parse_argv (pch, &argc, &argv, &err))
        {
          Log::add_err_va (_("Couldn't parse signature command \"%s\": %s"), pch, err->message);
          g_error_free (err);
        }
      }

      /* try to execute the file... */
      if (argc>0 && argv!=nullptr && argv[0]!=nullptr && g_file_test (argv[0], G_FILE_TEST_IS_EXECUTABLE))
      {
        char * spawn_stdout = nullptr;
        char * spawn_stderr = nullptr;
        int exit_status = 0;

        if (g_spawn_sync (nullptr, argv, nullptr, GSpawnFlags(0), nullptr, nullptr, &spawn_stdout, &spawn_stderr, &exit_status, nullptr))
          sig = spawn_stdout;
        if (spawn_stderr && *spawn_stderr)
          Log::add_err (spawn_stderr);

        g_free (spawn_stderr);
        g_free (spawn_stdout);
      }

      g_strfreev (argv);
    }
    else if (type == Profile::GPGSIG)
    {
      /// TODO : Perhaps show but omit in gmimemessage ??
    }

    /* Convert signature to UTF-8. Since the signature is a local file,
     * we assume the contents is in the user's locale's charset.
     * If we can't convert, clear the signature. Otherwise, we'd add an
     * charset-encoded sig (say 'iso-8859-1') to the body (in UTF-8),
     * which could result in a blank message in the composer window. */
    if (!sig.empty())
      sig = content_to_utf8 (sig);
    else
      Log::add_err (_("Couldn't convert signature to UTF-8."));

    if (!sig.empty())
      setme = sig;

    /* cleanup */
    g_free (pch);
  }
}

void
PostUI :: apply_profile ()
{
  apply_profile_to_body ();
  apply_profile_to_headers ();
}

namespace
{
  void replace (std::string        & in,
                const std::string  & from,
                const std::string  & to)
  {
    std::string out;
    std::string::size_type b(0), e(0);
    for (;;) {
      e = in.find (from, b);
      if (e == std::string::npos) {
        out.append (in, b, std::string::npos);
        break;
      } else {
        out.append (in, b, e-b);
        out.append (to);
        b = e + from.size();
      }
    }
    in = out;
  }

  bool do_attribution_substitutions (const StringView & mid,
                                     const StringView & date,
                                     const StringView & from,
                                     std::string& attrib)
  {
    if (mid.empty() && date.empty() && from.empty()) // not a follow-up; attribution not needed
      return false;

    const StringView brief = GNKSA :: get_short_author_name (from);
    replace (attrib, "%i", mid);
    replace (attrib, "%d", date);
    replace (attrib, "%a", from);
    replace (attrib, "%n", brief.to_string());
    return true;
  }
}

Profile
PostUI :: get_current_profile ()
{
  Profile profile;
  char * pch = gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(_from_combo));
  if (pch) {
    _profiles.get_profile (pch, profile);
    g_free (pch);
  }
  return profile;
}

void
PostUI :: apply_profile_to_body ()
{
  // get the selected profile
  const Profile profile (get_current_profile ());
  std::string attribution = profile.attribution;
  if (do_attribution_substitutions (_hidden_headers["X-Draft-Attribution-Id"],
                                    _hidden_headers["X-Draft-Attribution-Date"],
                                    _hidden_headers["X-Draft-Attribution-Author"],
                                    attribution))
    attribution = _tm.fill (attribution);
  else
    attribution.clear ();

  std::string body = get_body ();

  // replace the attribution
  const std::string old_attribution (_hidden_headers["X-Draft-Attribution"]);
  if (!attribution.empty())
  {
    // scrub the attribution for UTF8 cleanness
    attribution = header_to_utf8 (attribution);

    std::string::size_type pos = body.find (old_attribution);
    if (!old_attribution.empty() && (pos != std::string::npos))
      body.replace (pos, old_attribution.size(), attribution);
    else if (!attribution.empty())
      body = attribution + "\n\n" + body;
    _hidden_headers["X-Draft-Attribution"] = attribution;
  }

  // remove the last signature
  std::string::size_type pos = body.rfind (_current_signature);
  if (pos != body.npos) {
    body.resize (pos);
    StringView v (body);
    v.rtrim ();
    body.assign (v.str, v.len);
  }

  // get the new signature
  std::string sig;
  if (profile.use_sigfile && !profile.use_gpgsig) {
      load_signature (profile.signature_file, profile.sig_type, sig);
      int ignored;
      if (GNKSA::find_signature_delimiter (sig, ignored) == GNKSA::SIG_NONE)
        sig = std::string("\n\n-- \n") + sig;
  }
  _current_signature = sig;

  // add the new signature, and empty space between the
  // body (if present), the insert point, and the new signature
  int insert_pos;
  if (body.empty() && sig.empty()) {
    insert_pos = 0;
  } else if (body.empty()) {
    insert_pos = 0;
    body = "\n\n";
    body += sig;
  } else if (sig.empty()) {
    body += "\n\n";
    insert_pos = body.size();
  } else {
    body += "\n\n";
    insert_pos = body.size();
    body += "\n\n";
    body += sig;
  }

  GtkTextBuffer * buf (_body_buf);
  if (!body.empty())
    gtk_text_buffer_set_text (buf, body.c_str(), body.size());

  // set & scroll-to the insert point
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_offset (buf, &iter, insert_pos);
  gtk_text_buffer_move_mark_by_name (buf, "insert", &iter);
  gtk_text_buffer_move_mark_by_name (buf, "selection_bound", &iter);
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW(_body_view),
                                gtk_text_buffer_get_mark(buf, "insert"),
                                0.0, true, 0.0, 0.5);
}

void
PostUI :: apply_profile_to_headers ()
{
  // get the current `extra headers'
  GtkTextBuffer * buf (_headers_buf);
  Profile::headers_t headers;
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  char * text (gtk_text_buffer_get_text (buf,  &start, &end, false));
  StringView lines(text), line;
  while (lines.pop_token (line, '\n')) {
    StringView key, value(line);
    if (value.pop_token (key, ':')) {
      key.trim ();
      value.trim ();
      if (!key.empty() && !value.empty())
        headers[key] = value;
    }
  }
  g_free (text);

  // remove from current all the headers from the old profile.
  foreach_const (str2str_t, _profile_headers, it)
    if (headers.count(it->first) && headers[it->first]==it->second)
      headers.erase (it->first);

  // get the new profile
  const Profile profile (get_current_profile ());

  // add all the headers from the new profile.
  _profile_headers = profile.headers;
  foreach_const (Profile::headers_t, profile.headers, it)
    headers[it->first] = it->second;

  // if user has custom reply-to header, handle that here.
  Profile::headers_t::iterator p = headers.find ("Reply-To");
  if (p != headers.end()) {
    gtk_entry_set_text (GTK_ENTRY(_replyto_entry), p->second.c_str());
    headers.erase (p);
  }

  // rewrite the extra headers pane
  std::string s;
  foreach_const (Profile::headers_t, headers, it)
    s += it->first + ": " + it->second + "\n";
  if (!s.empty())
    gtk_text_buffer_set_text (buf, s.c_str(), s.size());
}

namespace
{
  void delete_post_ui (gpointer p)
  {
    delete static_cast<PostUI*>(p);
  }

  void on_from_combo_changed (GtkComboBox*, gpointer user_data)
  {
    static_cast<PostUI*>(user_data)->apply_profile ();
  }

  typedef std::map <std::string, std::string> str2str_t;

  struct SetMessageForeachHeaderData
  {
    str2str_t hidden_headers;
    std::string visible_headers;
  };

  void set_message_foreach_header_func (const char * name, const char * value, gpointer data_gpointer)
  {
    struct SetMessageForeachHeaderData * data (static_cast<SetMessageForeachHeaderData*>(data_gpointer));

    if (header_has_dedicated_entry (name))
    {
      // it's not an extra header
    }
    else if (extra_header_is_editable (name, value))
    {
      // it's a visible extra header.
      std::string& str (data->visible_headers);
      str += std::string(name) + ": " + value + "\n";
    }
    else
    {
      // it's a hidden extra header -- X-Draft-* headers, etc.
      data->hidden_headers[name] = value;
    }
  }
}

std::string
PostUI :: utf8ize (const StringView& in) const
{
  const char * local_charset = nullptr;
  g_get_charset (&local_charset);
  return content_to_utf8 (in, _charset.c_str(), local_charset);
}

#if GMIME_MINOR_VERSION == 4
namespace {
  inline GMimeStream* gmime_header_list_get_stream(GMimeHeaderList *hl)
  {
    // the name of this function was changed for 2.6
    return gmime_header_list_has_raw(hl);
  }
}
#endif

void
PostUI :: set_message (GMimeMessage * message)
{

  if (!message) return;

  // update our message header
  if (message)
    g_object_ref (G_OBJECT(message));
  if (_message)
    g_object_unref (G_OBJECT(_message));
  _message = message;

  // update subject, newsgroups, to fields
  std::string s = utf8ize (g_mime_message_get_subject (message));
  gtk_entry_set_text (GTK_ENTRY(_subject_entry), s.c_str());

  s = utf8ize (g_mime_object_get_header ((GMimeObject *) message, "Newsgroups"));
  gtk_entry_set_text (GTK_ENTRY(_groups_entry), s.c_str());

  s = utf8ize (g_mime_object_get_header ((GMimeObject *) message, "Followup-To"));
  gtk_entry_set_text (GTK_ENTRY(_followupto_entry), s.c_str());

  s = utf8ize (g_mime_object_get_header ((GMimeObject *) message, "Reply-To"));
  gtk_entry_set_text (GTK_ENTRY(_replyto_entry), s.c_str());

  InternetAddressList * addresses = g_mime_message_get_addresses (message, GMIME_ADDRESS_TYPE_TO);
  char * pch  = internet_address_list_to_string (addresses, nullptr, true);
  s = utf8ize (pch);
  gtk_entry_set_text (GTK_ENTRY(_to_entry), s.c_str());
  g_free (pch);

  // update 'other headers'
  SetMessageForeachHeaderData data;
  const char *name, *value;
  GMimeHeaderList HList;

//  FIXME: GMime 3.0
  int index_v = 0;
  int message_count = g_mime_header_list_get_count (message->mime_part->headers);

  if (message->mime_part && message_count) {
	do {
	  GMimeHeader *GMHeader = g_mime_header_list_get_header_at(message->mime_part->headers, index_v);
	  value = g_mime_header_get_value(GMHeader);
	  name = g_mime_header_get_name(GMHeader);
      set_message_foreach_header_func (name, value, &data);
      index_v++;
    } while ( index_v < message_count);
  }

  index_v = 0;
  message_count = g_mime_header_list_get_count (GMIME_OBJECT (message)->headers);

  if (message_count > 0) {
	do {
      GMimeHeader *GMHeader = g_mime_header_list_get_header_at(GMIME_OBJECT (message)->headers, index_v);
	  value = g_mime_header_get_value(GMHeader);
	  name = g_mime_header_get_name(GMHeader);
      set_message_foreach_header_func (name, value, &data);
      index_v++;
     } while ( index_v < message_count);
  }

  s = utf8ize (data.visible_headers);
  if (!s.empty())
    gtk_text_buffer_set_text (_headers_buf, s.c_str(), -1);
  _hidden_headers = data.hidden_headers;

  // update body
  int ignored;
  char * tmp = pan_g_mime_message_get_body (message, &ignored);
  s = utf8ize (tmp);
  g_free (tmp);
  if (!s.empty()) {
    if (_prefs.get_flag ("compose-wrap-enabled", true)) {
      s = TextMassager().fill (s);
      s += "\n\n";
    }
    if (!s.empty())
      gtk_text_buffer_set_text (_body_buf, s.c_str(), s.size());
  }

  // apply the profiles
  update_profile_combobox ();
  apply_profile ();

  // set focus to the first non-populated widget
  GtkWidget * grab (nullptr);
  if (!grab) {
    const char * cpch (gtk_entry_get_text (GTK_ENTRY(_subject_entry)));
    if (!cpch || !*cpch)
      grab = _subject_entry;
  }
  if (!grab) {
    const StringView one (gtk_entry_get_text (GTK_ENTRY(_groups_entry)));
    const StringView two (gtk_entry_get_text (GTK_ENTRY(_to_entry)));
    if (one.empty() && two.empty())
      grab = _groups_entry;
  }
  if (!grab)
    grab = _body_view;
  gtk_widget_grab_focus (grab);
}

/**
 * We hold off on setting the body textbuffer until after
 * the text view is realized so that GtkTreeView's text wrapping
 * will work properly.
 */
void
PostUI :: body_view_realized_cb (GtkWidget*, gpointer self_gpointer)
{
  PostUI * self = static_cast<PostUI*>(self_gpointer);

  /* import old draft from autosave file */
//  struct stat sb;
//  char *buf = g_build_filename(get_draft_filename().c_str(), "autosave", nullptr);
//  if (stat (buf, &sb)==0)
//    self->import_draft(buf);
//  g_free(buf);

  self->set_wrap_mode (self->_prefs.get_flag ("compose-wrap-enabled", false));
  self->set_message (self->_message);
  self->_unchanged_body = self->get_body ();

  if (self->_prefs.get_flag ("always-run-editor", false)) {
    self->spawn_editor ();
  }

  g_signal_handler_disconnect (self->_body_view, self->body_view_realized_handler);

  self->_realized = true;

  /* gpg stuff */
  const Profile profile (self->get_current_profile ());
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (self->_agroup, "gpg-sign")),profile.use_sigfile);
  user_has_gpg = profile.use_sigfile && profile.sig_type == Profile::GPGSIG;

  /* connect this here so signature isn't appended double */
  g_signal_connect (self->_from_combo, "changed", G_CALLBACK(on_from_combo_changed), self);
}

/***
****
***/

gboolean
PostUI :: group_entry_changed_idle (gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));
  std::string charset;

  // find the first posting charset in the newsgroups in _groups_entry.
  const char * text = gtk_entry_get_text (GTK_ENTRY(ui->_groups_entry));
  StringView line(text), groupname;
  while (line.pop_token (groupname, ',')) {
    groupname.trim ();
    if (groupname.empty())
      continue;
    charset = ui->_group_prefs.get_string (groupname, "character-encoding", "UTF-8");
    if (!charset.empty())
      break;
  }

  // if user hasn't specified a charset by hand,
  // use this one as the `default' charset for the groups being posted to.
  if (!charset.empty())
    ui->set_charset (charset);
  ui->_group_entry_changed_idle_tag = 0;
  return false;
}

void
PostUI :: group_entry_changed_cb (GtkEditable*, gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));
  unsigned int& tag (ui->_group_entry_changed_idle_tag);
  if (!tag)
    tag = g_timeout_add (2000, group_entry_changed_idle, ui);
}

gboolean
PostUI :: body_changed_idle (gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));

  ui->_body_changed_idle_tag = 0;
  unsigned int& tag (ui->_draft_autosave_idle_tag);
  if (!tag)
    tag = g_timeout_add_seconds( ui->_draft_autosave_timeout, draft_save_cb, ui);

  return false;
}

void
PostUI :: body_changed_cb (GtkEditable*, gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));
  unsigned int& tag (ui->_body_changed_idle_tag);
  if (!tag)
    tag = g_timeout_add (5000, body_changed_idle, ui);
}

/***
****
***/

namespace
{
  static void render_from (GtkCellLayout    * ,
                           GtkCellRenderer  * renderer,
                           GtkTreeModel     * model,
                           GtkTreeIter      * iter,
                           gpointer           profiles)
  {
    std::string from;
    std::string name;

    char * key (nullptr);
    gtk_tree_model_get (model, iter, 0, &key, -1);
    if (key) {
      name = key;
      Profile profile;
      if (static_cast<Profiles*>(profiles)->get_profile (key, profile))
        profile.get_from_header (from);
      g_free (key);
    }

    char * name_escaped = g_markup_escape_text (name.c_str(), name.size());
    char * from_escaped = g_markup_escape_text (from.c_str(), from.size());
    char * pch = g_strdup_printf ("%s - <i>%s</i>", from_escaped, name_escaped);
    g_object_set (renderer, "markup", pch, nullptr);
    g_free (pch);
    g_free (from_escaped);
    g_free (name_escaped);
  }
}

GtkWidget*
PostUI :: create_main_tab ()
{
  const GtkAttachOptions fill ((GtkAttachOptions)(GTK_FILL|GTK_EXPAND|GTK_SHRINK));
  char buf[512];
  int row = -1;
  GtkWidget *l, *w;
  GtkWidget *t = gtk_table_new (4, 2, false);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);

  // From

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("F_rom"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _from_combo = gtk_combo_box_text_new ();
  gtk_cell_layout_clear (GTK_CELL_LAYOUT(w));
  GtkCellRenderer * r =  gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(w), r, true);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT(w), r, render_from, &_profiles, nullptr);
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Subject

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Subject"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _subject_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Newsgroup

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Newsgroups"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _groups_entry = gtk_entry_new ();
  _group_entry_changed_id = g_signal_connect (w, "changed", G_CALLBACK(group_entry_changed_cb), this);
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Mail To

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("Mail _To"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _to_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Body
  w = create_body_widget (_body_buf, _body_view, _prefs);
  set_spellcheck_enabled (_prefs.get_flag ("spellcheck-enabled", DEFAULT_SPELLCHECK_FLAG));
  _body_changed_id = g_signal_connect (_body_buf, "changed", G_CALLBACK(body_changed_cb), this);

  GtkWidget * v = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD);
  gtk_container_set_border_width (GTK_CONTAINER(v), PAD);
  gtk_box_pack_start (GTK_BOX(v), t, false, false, 0);
  pan_box_pack_start_defaults (GTK_BOX(v), w);
  return v;
}

void
PostUI :: message_id_toggled_cb (GtkToggleButton * tb, gpointer pointer)
{
  PostUI* post = static_cast<PostUI*>(pointer);
  post->_prefs.set_flag (MESSAGE_ID_PREFS_KEY, gtk_toggle_button_get_active(tb));
}

namespace
{
  void user_agent_toggled_cb (GtkToggleButton * tb, gpointer pointer)
  {
    Prefs* prefs = static_cast<Prefs*>(pointer);
    prefs->set_flag (MESSAGE_ID_PREFS_KEY, gtk_toggle_button_get_active(tb));
  }
}

namespace
{
  GtkWidget * add_button (GtkWidget   * box,
                                      const gchar * icon_name,
                                      GCallback     callback,
                                      gpointer      user_data)
  {
    GtkWidget * w = gtk_button_new_from_icon_name (icon_name, GTK_ICON_SIZE_BUTTON);
    gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);
    if (callback)
      g_signal_connect (w, "clicked", callback, user_data);
    gtk_box_pack_start (GTK_BOX(box), w, false, false, 0);
    return w;
  }

  void
  render_filename (GtkTreeViewColumn * ,
                   GtkCellRenderer   * renderer,
                   GtkTreeModel      * model,
                   GtkTreeIter       * iter,
                   gpointer)
  {

    TaskUpload* fd(nullptr);
    gtk_tree_model_get (model, iter, 2, &fd, -1);
    if (fd)
      g_object_set (renderer, "text", fd->get_basename().c_str(), nullptr);
  }

  void
  render_row_number (GtkTreeViewColumn * ,
                   GtkCellRenderer   * renderer,
                   GtkTreeModel      * model,
                   GtkTreeIter       * iter,
                   gpointer)
  {

    GtkTreePath * path = gtk_tree_model_get_path ( model , iter ) ;
    int cnt (gtk_tree_path_get_indices ( path )[0]+1) ;
    std::string tmp;
    char buf[256];
    g_snprintf(buf,sizeof(buf),"%d",cnt);
    g_object_set (renderer, "text", buf, nullptr);
  }

}

GtkWidget*
PostUI :: create_filequeue_tab ()
{
  GtkWidget *w ;
  GtkListStore *list_store;
  GtkCellRenderer *renderer;
  GtkWidget * vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget * buttons = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);

  // add button row
  // icon name list: https://commons.wikimedia.org/wiki/GNOME_Desktop_icons
  add_button (buttons, "go-up", G_CALLBACK(up_clicked_cb), this);
  add_button (buttons, "go-top", G_CALLBACK(top_clicked_cb), this);
  gtk_box_pack_start (GTK_BOX(buttons), gtk_separator_new(GTK_ORIENTATION_VERTICAL), 0, 0, 0);
  add_button (buttons, "go-down", G_CALLBACK(down_clicked_cb), this);
  add_button (buttons, "go-bottom", G_CALLBACK(bottom_clicked_cb), this);
  gtk_box_pack_start (GTK_BOX(buttons), gtk_separator_new(GTK_ORIENTATION_VERTICAL), 0, 0, 0);
  w = add_button (buttons, "edit-cut", G_CALLBACK(delete_clicked_cb), this);
  gtk_widget_set_tooltip_text( w, _("Delete from Queue"));
  pan_box_pack_start_defaults (GTK_BOX(buttons), gtk_event_box_new());

  gtk_box_pack_start (GTK_BOX(vbox), buttons, false, false, 0);
  gtk_box_pack_start (GTK_BOX(vbox), gtk_separator_new(GTK_ORIENTATION_HORIZONTAL), false, false, 0);

  //add filestore
  list_store = gtk_list_store_new (4, G_TYPE_UINT, G_TYPE_STRING, G_TYPE_POINTER, G_TYPE_STRING);
  w = _filequeue_store = gtk_tree_view_new_with_model (GTK_TREE_MODEL(list_store));

  // add columns
  renderer = gtk_cell_renderer_text_new ();
  GtkTreeView * t = GTK_TREE_VIEW(w);
  gtk_tree_view_insert_column_with_data_func(t, 0, (_("No.")), renderer, render_row_number, nullptr, nullptr);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_attributes (t, 1,(_("Subject")),renderer,"text", 1,nullptr);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_data_func(t, 2, (_("Filename")), renderer, render_filename, nullptr, nullptr);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_attributes (t, 3, (_("Size (KB)")),renderer,"text", 3,nullptr);

  // connect signals for popup menu
  g_signal_connect (w, "popup-menu", G_CALLBACK(on_popup_menu), this);
  g_signal_connect (w, "button-press-event", G_CALLBACK(on_button_pressed), this);

  //set hint and selection
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(w),TRUE);
  GtkTreeSelection * selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (w));
  g_signal_connect (selection, "changed", G_CALLBACK(on_selection_changed), this);
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(w));

  //append scroll window
  w = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), _filequeue_store);
  gtk_box_pack_start (GTK_BOX(vbox), w, true, true, 0);

  // add status bar
  gtk_box_pack_start (GTK_BOX(vbox), create_filequeue_status_bar(), false, false, 0);
  update_filequeue_label ();

  return vbox;
}

GtkWidget*
PostUI:: create_filequeue_status_bar ()
{
  GtkWidget * w;
  GtkWidget * status_bar (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));

  // connection status
  w = _filequeue_label = gtk_label_new (nullptr);
  gtk_misc_set_padding (GTK_MISC(w), PAD, 0);
  GtkWidget * frame = gtk_frame_new (nullptr);
  gtk_container_set_border_width (GTK_CONTAINER(frame), 0);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(frame), w);
  gtk_box_pack_start (GTK_BOX(status_bar), frame, FALSE, FALSE, 0);

  return status_bar;
}

void
PostUI:: on_parts_box_clicked_cb (GtkCellRendererToggle *cell, gchar *path_str, gpointer user_data)
{
  PostUI* data(static_cast<PostUI*>(user_data));
  GtkWidget    * w    = data->parts_store() ;
  GtkTreeModel * model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
  int part(42);
  GtkTreeIter  iter;
  GtkTreePath *path = gtk_tree_path_new_from_string (path_str);
  gboolean enabled;

  gtk_tree_model_get_iter (model, &iter, path);
  gtk_tree_model_get (model, &iter, 0, &part, 1, &enabled, -1);

  enabled ^= 1;
  if (enabled==0)
    data->_upload_ptr->_wanted.erase(part);
  else
    data->_upload_ptr->_wanted.insert(part);

  gtk_list_store_set(GTK_LIST_STORE( model ), &iter, 1, enabled, -1);
  gtk_tree_path_free (path);
  data->update_parts_tab();
}


GtkWidget*
PostUI :: create_parts_tab ()
{

  const GtkAttachOptions fill ((GtkAttachOptions)(GTK_FILL));
  const GtkAttachOptions fe ((GtkAttachOptions)(GTK_FILL|GTK_EXPAND|GTK_SHRINK));

  GtkWidget *w, *l;
  char buf[512];
  int row = -1;
  GtkCellRenderer * renderer;
  GtkWidget *t = gtk_table_new (8, 2, false);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);

  ++row;
  l = gtk_label_new (nullptr);
  gtk_table_attach (GTK_TABLE(t), l, 0, 2, row, row+1, fe, fill, 0, 0);

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("Filename"));
  l = gtk_label_new (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);
  g_snprintf (buf, sizeof(buf), "%s", _upload_ptr->_basename.c_str());
  l = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL(l), 0.5f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_widget_set_tooltip_text (l, _("The current filename"));
  gtk_table_attach (GTK_TABLE(t), l, 1, 2, row, row+1, fe, fill, 0, 0);

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("Subject Line"));
  l = gtk_label_new (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);
  g_snprintf (buf, sizeof(buf), "%s", _upload_ptr->_subject.c_str());
  l = gtk_label_new (buf);
  gtk_label_set_xalign (GTK_LABEL(l), 0.5f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_widget_set_tooltip_text (l, _("The current subject line"));
  gtk_table_attach (GTK_TABLE(t), l, 1, 2, row, row+1, fe, fill, 0, 0);

  ++row;
  l = gtk_label_new (nullptr);
  gtk_table_attach (GTK_TABLE(t), l, 0, 2, row, row+1, fe, fill, 0, 0);

  //treeview for parts list
  ++row;
  w = _parts_store = gtk_tree_view_new_with_model (GTK_TREE_MODEL(gtk_list_store_new (3,  G_TYPE_UINT, G_TYPE_BOOLEAN, G_TYPE_STRING)));

  // add columns
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (w), 0,
                          (_("No. ")),renderer,"text", 0,nullptr);
  renderer = gtk_cell_renderer_toggle_new();
  g_signal_connect (G_OBJECT(renderer), "toggled", G_CALLBACK(on_parts_box_clicked_cb), this);
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (w), 1,
                          (_("Enable/Disable")),renderer,"active", 1,nullptr);
  renderer = gtk_cell_renderer_text_new ();
  gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (w), 2,
                          (_("Filename")),renderer,"text", 2,nullptr);

  //set hint and selection
  gtk_tree_view_set_rules_hint(GTK_TREE_VIEW(w),TRUE);
  gtk_tree_view_columns_autosize(GTK_TREE_VIEW(w));

  gtk_table_attach (GTK_TABLE(t), w, 0, 2, row, row+1, fe, fill, 0, 0);

  //append scroll window
  w = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW(w), t);

  return w;
}

GtkWidget*
PostUI :: create_extras_tab ()
{
  const GtkAttachOptions fill ((GtkAttachOptions)(GTK_FILL));
  const GtkAttachOptions fe ((GtkAttachOptions)(GTK_FILL|GTK_EXPAND|GTK_SHRINK));
  char buf[512];
  int row = -1;
  GtkWidget *l, *w;
  GtkWidget *t = gtk_table_new (3, 2, false);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);

  // Followup-To

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("Follo_wup-To"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _followupto_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  /* i18n: "poster" is a key used by many newsreaders.  probably safest to keep this key in english. */
  gtk_widget_set_tooltip_text (w, _("The newsgroups where replies to your message should go.  This is only needed if it differs from "
      "the \"Newsgroups\" header.\n\nTo direct all replies to your email address, use \"Followup-To: poster\""));
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fe, fill, 0, 0);

  //  Reply-To

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Reply-To"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _replyto_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_widget_set_tooltip_text (w, _("The email account where mail replies to your posted message should go. "
    "This is only needed if it differs from the \"From\" header."));
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fe, fill, 0, 0);

  //  Extra Headers

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Custom Headers"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_label_set_xalign (GTK_LABEL(l), 0.0f);
  gtk_label_set_yalign (GTK_LABEL(l), 0.0f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  ++row;
  w = gtk_text_view_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  _headers_buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW(w));
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(w), GTK_WRAP_NONE);
  gtk_text_view_set_editable (GTK_TEXT_VIEW(w), true);
  GtkWidget * scroll = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER(scroll), w);
  GtkWidget * frame = gtk_frame_new (nullptr);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(frame), scroll);
  gtk_table_attach_defaults (GTK_TABLE(t), frame, 0, 2, row, row+1);


  //  User-Agent

  ++row;
  w = _user_agent_check = gtk_check_button_new_with_mnemonic (_("Add \"_User-Agent\" header"));
  bool b = _prefs.get_flag (USER_AGENT_PREFS_KEY, true);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), b);
  g_signal_connect (w, "toggled", G_CALLBACK(user_agent_toggled_cb), &_prefs);
  gtk_table_attach (GTK_TABLE(t), w, 0, 2, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  ++row;
  w = _message_id_check = gtk_check_button_new_with_mnemonic (_("Add \"Message-_ID\" header"));
  b = _prefs.get_flag(MESSAGE_ID_PREFS_KEY,false);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), b);
  g_signal_connect (w, "toggled", G_CALLBACK(message_id_toggled_cb), this);
  gtk_table_attach (GTK_TABLE(t), w, 0, 2, row, row+1, GTK_FILL, GTK_FILL, 0, 0);


  gtk_container_set_border_width (GTK_CONTAINER(t), PAD);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);
  gtk_table_set_row_spacings (GTK_TABLE(t), PAD);
  return t;
}

void
PostUI :: get_selected_files_foreach (GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer list_g)
{
  TaskUpload* file(nullptr);
  gtk_tree_model_get (model, iter, 2, &file, -1);
  static_cast<PostUI::tasks_t*>(list_g)->push_back (file);
}

PostUI::tasks_t
PostUI :: get_selected_files () const
{
  PostUI::tasks_t tasks;
  GtkTreeView * view (GTK_TREE_VIEW (_filequeue_store));
  GtkTreeSelection * sel (gtk_tree_view_get_selection (view));
  gtk_tree_selection_selected_foreach (sel, get_selected_files_foreach, &tasks);
  return tasks;
}

void
PostUI :: remove_files (void)
{
  _upload_queue.remove_tasks (get_selected_files());
}

void
PostUI :: move_up (void)
{
  _upload_queue.move_up (get_selected_files());
}

void
PostUI :: move_down (void)
{
  _upload_queue.move_down (get_selected_files());
}

void
PostUI :: move_top (void)
{
  _upload_queue.move_top (get_selected_files());
}

void
PostUI :: move_bottom (void)
{
  _upload_queue.move_bottom (get_selected_files());
}


int
PostUI :: get_total_parts(const char* file)
{
    struct stat sb;
    stat (file,&sb);
    int max (std::max(1,(int)std::ceil((double)sb.st_size /
                                       (double)_prefs.get_int_min("upload-option-bpf",512*1024))));
    return max;
}

void
PostUI :: clear_list (void)
{
  _upload_queue.clear();
  update_filequeue_label();
}

void PostUI :: up_clicked_cb (GtkButton*, PostUI* pane)
{
  pane->move_up ();
}
void PostUI :: down_clicked_cb (GtkButton*, PostUI* pane)
{
  pane->move_down ();
}
void PostUI :: top_clicked_cb (GtkButton*, PostUI* pane)
{
  pane->move_top ();
}
void PostUI :: bottom_clicked_cb (GtkButton*, PostUI* pane)
{
  pane->move_bottom ();
}
void PostUI :: delete_clicked_cb (GtkButton*, PostUI* pane)
{
  pane->remove_files ();
}

PostUI :: ~PostUI ()
{
  if (_group_entry_changed_idle_tag)
    g_source_remove (_group_entry_changed_idle_tag);
  if (_body_changed_idle_tag)
    g_source_remove (_body_changed_idle_tag);
  if (_draft_autosave_idle_tag)
    g_source_remove (_draft_autosave_idle_tag);

  g_object_unref (G_OBJECT(_message));

  _upload_queue.remove_listener (this);
}



void
PostUI :: select_parts ()
{

  PostUI::tasks_t set(get_selected_files());
  if (set.empty()) return;
  _upload_ptr = dynamic_cast<TaskUpload*>(set[0]);
  if (!_upload_ptr) return;

  GtkWidget * w = _part_select = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (_part_select, "delete-event", G_CALLBACK(delete_parts_cb), this);
  gtk_window_set_role (GTK_WINDOW(w), "pan-parts-window");
  gtk_window_set_title (GTK_WINDOW(w), _("Select Parts"));
  int x,y;

  // FIXME (sometimes....) BUG: Native Windows wider or taller than 65535 pixels are not supported
  x = _prefs.get_int("post-ui-width", 450);
  y = _prefs.get_int("post-ui-height", 450);

  gtk_window_set_default_size (GTK_WINDOW(w), x, y);
  // populate the window
  GtkWidget * vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD_SMALL);
  gtk_container_add (GTK_CONTAINER(w), vbox);

  GtkWidget * notebook = gtk_notebook_new ();
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), create_parts_tab(), gtk_label_new_with_mnemonic(_("_Parts")));
  pan_box_pack_start_defaults (GTK_BOX(vbox), notebook);

  gtk_widget_show_all (w);
  update_parts_tab();

}

void
PostUI :: update_parts_tab()
{
  GtkWidget    * w    = _parts_store ;
  GtkListStore *store = GTK_LIST_STORE(
                        gtk_tree_view_get_model(GTK_TREE_VIEW(w)));
  GtkTreeIter   iter;
  gtk_list_store_clear(store);
  gboolean res(FALSE);

  GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
  g_object_ref(model);
  gtk_tree_view_set_model(GTK_TREE_VIEW(w), nullptr);

  for (int i=1;i<=_upload_ptr->_total_parts;++i)
  {
    gtk_list_store_append (store, &iter);
    res = (_upload_ptr->_wanted.find(i) != _upload_ptr->_wanted.end()) ? TRUE : FALSE;
    gtk_list_store_set (store, &iter,
                        0, i,
                        1, res,
                        2, _upload_ptr->_basename.c_str(),
                        -1);
  }
  gtk_tree_view_set_model(GTK_TREE_VIEW(w), model);
  g_object_unref(model);
}

gboolean
PostUI::draft_save_cb(gpointer ptr)
{
    PostUI *data = static_cast<PostUI*>(ptr);
    if (!data) return false;
    GMimeMessage * msg = data->new_message_from_ui (DRAFTING);
    std::string& draft_filename (get_draft_filename ());
    char * filename = g_build_filename (draft_filename.c_str(), "autosave", nullptr);

    std::ofstream o (filename);
    char * headers (g_mime_object_get_headers ((GMimeObject *) msg, nullptr));
    o << headers;
    const std::string body (data->get_body ());
    o << body;
    o.close ();

    g_object_unref (msg);
    g_free(filename);

    data->_unchanged_body = body;
    data->_draft_autosave_idle_tag = 0;
    return false;
}

gboolean
PostUI :: on_keyboard_key_pressed_cb (GtkWidget * w, GdkEventKey * event, gpointer ptr)
{
  PostUI *data = static_cast<PostUI*>(ptr);
  if (!data) return false;
  if (event->type == GDK_KEY_PRESS)
  {
    if (event->keyval == GDK_KEY_Delete)
    {
      if (gtk_notebook_get_current_page(GTK_NOTEBOOK(data->_notebook)) == PostUI::PAGE_QUEUE)
        data->remove_files();
    }
  }
  return false;
}

PostUI :: PostUI (GtkWindow    * parent,
                  Data         & data,
                  Queue        & queue,
                  GroupServer  & gs,
                  Profiles     & profiles,
                  GMimeMessage * message,
                  Prefs        & prefs,
                  GroupPrefs   & group_prefs,
                  EncodeCache  & cache):
  _data (data),
  _queue (queue),
  _gs (gs),
  _profiles (profiles),
  _prefs (prefs),
  _group_prefs (group_prefs),
  _root (),
  _part_select(nullptr),
  _from_combo (nullptr),
  _subject_entry (nullptr),
  _groups_entry (nullptr),
  _filequeue_store(nullptr),
  _parts_store(nullptr),
  _to_entry (nullptr),
  _followupto_entry (nullptr),
  _replyto_entry (nullptr),
  _body_view (nullptr),
  _user_agent_check(nullptr),
  _message_id_check(nullptr),
  _body_buf (nullptr),
  _headers_buf(nullptr),
  _message (message),
  _charset (DEFAULT_CHARSET),
  _uim(nullptr),
  _post_task(nullptr),
  _wrap_pixels(0),
  _enc(GMIME_CONTENT_ENCODING_8BIT),
  _file_queue_empty(true),
  _upload_ptr(nullptr),
  _total_parts(0),
  _uploads(0),
  _realized(false),
  _agroup(nullptr),
  //body_view_realized_handler is set up in the code below
  _filequeue_eventbox (nullptr),
  _filequeue_label (nullptr),
  _body_changed_id(0),
  _body_changed_idle_tag(0),
  _group_entry_changed_idle_tag (0),
  _group_entry_changed_id (0),
  _cache (cache),
  _spawner_action(nullptr),
  _running_uploads(0),
  _draft_autosave_id(0),
  _draft_autosave_timeout(0),
  _draft_autosave_idle_tag(0),
  _notebook(nullptr),
  delete_override(0)
{

  _upload_queue.add_listener (this);

  /* init timer for autosave */
//  set_draft_autosave_timeout( prefs.get_int("draft-autosave-timeout-min", 10 ));
//  _draft_autosave_id = g_timeout_add_seconds( _draft_autosave_timeout * 60, draft_save_cb, this);

  g_assert (profiles.has_profiles());
  g_return_if_fail (message != nullptr);

  #ifdef HAVE_GSPELL
  // set the spellchecker language according to the first destination newsgroup's options
  StringView line (g_mime_object_get_header ((GMimeObject *) message, "Newsgroups"));
  StringView groupname;
  // get the first newsgroup
  while (line.pop_token (groupname, ',')) {
    groupname.trim ();
    if (groupname.empty())
      continue;
    // set the language as defined in the newsgroup's options or, if it doesn't have one, the system locale
    _spellcheck_language = group_prefs.get_string (groupname, "spellcheck-language", "");

    if (!_spellcheck_language.empty())
      break;
  }
  #endif

  // create the window
  _root = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (_root, "delete-event", G_CALLBACK(delete_event_cb), this);
  gtk_window_set_role (GTK_WINDOW(_root), "pan-post-window");
  gtk_window_set_title (GTK_WINDOW(_root), _("Post Article"));
  int w,h;
  w = _prefs.get_int("post-ui-width", 450);
  h = _prefs.get_int("post-ui-height", 450);
  gtk_window_set_default_size (GTK_WINDOW(_root), w, h);
  g_object_set_data_full (G_OBJECT(_root), "post-ui", this, delete_post_ui);
  if (parent) {
    gtk_window_set_transient_for (GTK_WINDOW(_root), parent);
    gtk_window_set_position (GTK_WINDOW(_root), GTK_WIN_POS_CENTER_ON_PARENT);
  }

  // populate the window
  GtkWidget * vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD_SMALL);
  GtkWidget * menu_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX(vbox), menu_vbox, false, false, 0);
  add_actions (menu_vbox);
  gtk_window_add_accel_group (GTK_WINDOW(_root), gtk_ui_manager_get_accel_group (_uim));
  gtk_widget_show_all (vbox);
  gtk_container_add (GTK_CONTAINER(_root), vbox);

  GtkWidget * notebook = _notebook = gtk_notebook_new ();
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), create_main_tab(), gtk_label_new_with_mnemonic(_("_Message")));
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), create_extras_tab(), gtk_label_new_with_mnemonic(_("More _Headers")));
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), create_filequeue_tab(), gtk_label_new_with_mnemonic(_("File _Queue")));
  pan_box_pack_start_defaults (GTK_BOX(vbox), notebook);

  /* button press handler for "delete" */
  g_signal_connect (vbox, "key-press-event", G_CALLBACK(on_keyboard_key_pressed_cb), this);

  // remember this message, but don't put it in the text view yet.
  // we have to wait for it to be realized first so that wrapping
  // will work correctly.
  g_object_ref (G_OBJECT(_message));
  body_view_realized_handler = g_signal_connect (_body_view, "realize", G_CALLBACK(body_view_realized_cb), this);

}

PostUI*
PostUI :: create_window (GtkWindow    * parent,
                         Data         & data,
                         Queue        & queue,
                         GroupServer  & gs,
                         Profiles     & profiles,
                         GMimeMessage * message,
                         Prefs        & prefs,
                         GroupPrefs   & group_prefs,
                         EncodeCache  & cache)
{
  // can't post without a profile...
  if (!profiles.has_profiles())
  {
    Profile profile;
    profile.username = g_get_real_name ();
    /* xgettext: no-c-format */
    profile.attribution = _("On %d, %n wrote:");
    ProfileDialog d (data, g_get_real_name(), profile, GTK_WINDOW(parent));
    const bool got_profile (ProfileDialog :: run_until_valid_or_cancel (d));
    if (got_profile) {
      std::string name;
      d.get_profile (name, profile);
      profiles.add_profile (name, profile);
    }
    gtk_widget_destroy (d.root());
    if (!got_profile)
      return nullptr;
  }

  return new PostUI (nullptr, data, queue, gs, profiles, message, prefs, group_prefs, cache);
}

void
PostUI :: prompt_user_for_queueable_files (GtkWindow * parent, const Prefs& prefs)
{
  const Profile profile (get_current_profile ());
  PostUI::tasks_t tasks;
  GMimeMessage * tmp (new_message_from_ui (UPLOADING));
  if (!check_message(profile.posting_server, tmp, true))
  {
    g_object_unref (G_OBJECT(tmp));
    return;
  }

  std::string prev_path = prefs.get_string ("default-save-attachments-path", g_get_home_dir ());
  GtkWidget * w = gtk_file_chooser_dialog_new (_("Add files to queue"),
				      parent,
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				      nullptr);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (w), prev_path.c_str());
	gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (w), true);
	gtk_file_chooser_set_show_hidden (GTK_FILE_CHOOSER (w), false);

	const int response (gtk_dialog_run (GTK_DIALOG(w)));
	if (response == GTK_RESPONSE_ACCEPT) {
    GSList * tmp_list = gtk_file_chooser_get_filenames(GTK_FILE_CHOOSER (w));
    gtk_widget_destroy (w);

    TaskUpload::UploadInfo ui;
    // query lines per file value
    ui.bpf = _prefs.get_int("upload-option-bpf",512*1024);

    std::string author;
    profile.get_from_header(author);
    std::string subject(utf8ize (g_mime_message_get_subject (tmp)));

    // insert groups info from msg
    quarks_t groups;
    StringView line (g_mime_object_get_header ((GMimeObject *) tmp, "Newsgroups"));
    StringView groupname;
    while (line.pop_token (groupname, ',')) {
      groupname.trim ();
      if (!groupname.empty())
          groups.insert(Quark(groupname));
    }

    GSList * cur = g_slist_nth (tmp_list,0);
    std::vector<Task*> uploads;
    for (; cur; cur = cur->next)
    {
      GMimeMessage * msg (new_message_from_ui (UPLOADING));
      TaskUpload* tmp;
      Article a;
      a.subject = subject;
      a.author = author;
      foreach_const (quarks_t, groups, git)
         a.xref.insert (profile.posting_server, *git, static_cast<Article_Number>(0));
      ui.total = get_total_parts((const char*)cur->data);
      tmp = new TaskUpload((const char*)cur->data,
                        profile.posting_server, _cache, a, ui, msg);

      // insert wanted parts to upload
      for (int i=1;i<=ui.total; ++i)
        tmp->_wanted.insert(i);
      uploads.push_back(tmp);
    }

     _upload_queue.add_tasks(uploads);
    if (_file_queue_empty) _file_queue_empty= false;
    g_slist_free (tmp_list);
  } else
    gtk_widget_destroy (w);
  g_object_unref (G_OBJECT(tmp));

}

std::string
PostUI :: prompt_user_for_upload_nzb_dir (GtkWindow * parent, const Prefs& prefs)
{
  char buf[4096];
  std::string path;

  std::string prev_path = prefs.get_string ("default-save-attachments-path", g_get_home_dir ());
  if (!file :: file_exists (prev_path.c_str()))
    prev_path = g_get_home_dir ();
  const char * cpch (gtk_entry_get_text (GTK_ENTRY(_subject_entry)));
  g_snprintf(buf,sizeof(buf), "%s.nzb", cpch ? cpch : "_(Untitled)");
  std::string prev_file(buf);

  GtkWidget * w = gtk_file_chooser_dialog_new (_("Save Upload Queue as NZB File"),
                                                GTK_WINDOW(parent),
                                                GTK_FILE_CHOOSER_ACTION_SAVE,
                                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                                nullptr);
  gtk_dialog_set_default_response (GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (w), TRUE);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (w), prev_path.c_str());
  gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (w), prev_file.c_str());

  GtkFileFilter * filter = gtk_file_filter_new ();
  gtk_file_filter_add_pattern (filter, "*.[Nn][Zz][Bb]");
  gtk_file_filter_set_name (filter, _("NZB Files"));
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(w), filter);
  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER(w), false);
  if (GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(w)))
  {
    char * tmp = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (w));
    path = tmp;
    g_free (tmp);
    //remove old file
    unlink(path.c_str());
  } else
    path.clear();

  gtk_widget_destroy (w);
  return path;
}
