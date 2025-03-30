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

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/tasks/task-article.h>
#include <pan/tasks/queue.h>
#include <pan/usenet-utils/text-massager.h>
#include <pan/data-impl/article-rules.h>
#include "hig.h"
#include "pad.h"
#include "pan-file-entry.h"
#include "save-ui.h"


using namespace pan;

namespace
{

  void
  show_group_substitution_help_dialog (gpointer window)
  {
    const char * str = _("%g - group as one directory (alt.binaries.pictures.trains)\n"
                         "%G - group as nested directory (/alt/binaries/pictures/trains)\n"
                         "%s - subject line excerpt\n"
                         "%S - subject line\n"
                         "%n - Poster display name\n"
                         "%e - Poster email address\n"
                         "%d - Article timestamp\n"
                         " \n"
                         "\"/home/user/News/Pan/%g\" becomes\n"
                         "\"/home/user/News/Pan/alt.binaries.pictures.trains\", and\n"
                         "\"/home/user/News/Pan/%G\" becomes\n"
                         "\"/home/user/News/Pan/alt/binaries/pictures/trains\",");
    GtkWidget * w = gtk_message_dialog_new (GTK_WINDOW(window),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_CLOSE, "%s", str);
    g_signal_connect_swapped (w, "response",
                              G_CALLBACK(gtk_widget_destroy), w);
    gtk_widget_show_all (w);
  }

  void delete_save_dialog (gpointer castme)
  {
    delete static_cast<SaveDialog*>(castme);
  }

  enum { PATH_GROUP, PATH_ENTRY };

  int path_mode (PATH_GROUP);
}

void
SaveDialog :: response_cb (GtkDialog * dialog,
                           int response,
                           gpointer user_data)
{

  if (response == GTK_RESPONSE_OK)
  {
    SaveDialog * self (static_cast<SaveDialog*>(user_data));

    bool subject_in_path = false;

    // set the path mode based on what widgets exist & are set
    GtkWidget * gr (self->_save_group_path_radio);
    GtkWidget * er (self->_save_custom_path_radio);
    if (gr && er)
      path_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gr)) ? PATH_GROUP : PATH_ENTRY;
    else
      path_mode = PATH_ENTRY;

    // get the save path
    std::string path, opath;
    if (path_mode == PATH_GROUP)
      path = (const char*) g_object_get_data (G_OBJECT(gr), "default-group-save-path");
    else if (path_mode == PATH_ENTRY)  {
      path = pan :: file_entry_get (self->_save_path_entry);
      self->_prefs.set_string ("default-save-attachments-path", path);
    }
    path = opath = expand_download_dir (path.c_str(), self->_group.to_view());
    if ((path.find("%s") != path.npos) || (path.find("%S") != path.npos))
      subject_in_path = true;

    // get the save mode
    int save_mode;
    std::string s (self->_prefs.get_string ("save-article-mode", "save-attachments"));
    if      (s == "save-text")                 save_mode = TaskArticle::RAW;
    else if (s == "save-attachments-and-text") save_mode = TaskArticle::DECODE | TaskArticle::RAW;
    else                                       save_mode = TaskArticle::DECODE;

    std::string sep( self->_prefs.get_string("save-subj-separator", "-") );

    const bool always (self->_prefs.get_flag("mark-downloaded-articles-read", false));

    // make the tasks...
    Queue::tasks_t tasks;
    foreach_const (std::vector<Article>, self->_articles, it)
    {
      if (subject_in_path)
        path = expand_download_dir_subject(opath.c_str(), it->subject, sep);
      tasks.push_back (new TaskArticle (self->_server_rank,
                                        self->_group_server,
                                        *it,
                                        self->_cache,
                                        self->_read,
                                        always ? TaskArticle::ALWAYS_MARK : TaskArticle::NEVER_MARK,
                                        nullptr,
                                        TaskArticle::SaveMode(save_mode),
                                        path));
    }

    // get the queue mode...
    Queue::AddMode queue_mode;
    s = self->_prefs.get_string ("save-article-priority", "age");
    if      (s == "top")    queue_mode = Queue::TOP;
    else if (s == "bottom") queue_mode = Queue::BOTTOM;
    else                    queue_mode = Queue::AGE;

    // queue up the tasks...
    if (!tasks.empty())
      self->_queue.add_tasks (tasks, queue_mode);
  }

  gtk_widget_destroy (GTK_WIDGET(dialog));
}

namespace
{
  void entry_changed_cb (GtkEditable*, gpointer radio_or_null)
  {
    if (radio_or_null)
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(radio_or_null), true);
  }

  void combo_box_selection_changed (GtkComboBox * combo, gpointer prefs, const char * key)
  {
    GtkTreeIter iter;
    gtk_combo_box_get_active_iter (combo, &iter);
    GtkTreeModel * model (gtk_combo_box_get_model (combo));
    char * s (nullptr);
    gtk_tree_model_get (model, &iter, 0, &s, -1);
    static_cast<Prefs*>(prefs)->set_string (key, s);
    g_free (s);
  }

  void mode_combo_box_selection_changed (GtkComboBox * combo, gpointer prefs)
  {
    combo_box_selection_changed (combo, prefs, "save-article-mode");
  }

  void priority_combo_box_selection_changed (GtkComboBox * combo, gpointer prefs)
  {
    combo_box_selection_changed (combo, prefs, "save-article-priority");
  }

  GtkWidget* create_combo_box (Prefs& prefs, const char * key, const char * fallback, ...)
  {
    const std::string active_str (prefs.get_string (key, fallback));
    int active (-1);
    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
    va_list args;
    va_start (args, fallback);
    for (int i=0;; ++i) {
      const char * key_str = va_arg (args, const char*);
      if (!key_str) break;
      const char * gui_str = va_arg (args, const char*);
      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, key_str, 1, gui_str, -1);
      if (active_str == key_str) active = i;
    }
    va_end(args);
    GtkWidget * w = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, true);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 1, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(w), active);
    return w;
  }

  GtkWidget* create_mode_combo_box (Prefs& prefs)
  {
    return create_combo_box (prefs, "save-article-mode", "save-attachments",
                             "save-attachments", _("Save attachments"),
                             "save-text", _("Save text"),
                             "save-attachments-and-text", _("Save attachments and text"),
                             nullptr);
  }

  GtkWidget* create_priority_combo_box (Prefs& prefs)
  {
    return create_combo_box (prefs, "save-article-priority", "age",
                             "age", _("Add to the queue sorted by date posted"),
                             "top", _("Add to the front of the queue"),
                             "bottom", _("Add to the back of the queue"),
                             nullptr);
  }
}

SaveDialog :: SaveDialog (Prefs                       & prefs,
                          const GroupPrefs            & group_prefs,
                          const ServerRank            & server_rank,
                          const GroupServer           & group_server,
                          ArticleCache                & cache,
                          ArticleRead                 & read,
                          Queue                       & queue,
                          GtkWindow                   * parent_window,
                          const Quark                 & group,
                          const std::vector<Article>  & articles):
  _prefs(prefs),
  _server_rank (server_rank),
  _group_server (group_server),
  _cache (cache),
  _read (read),
  _queue (queue),
  _group (group),
  _root (nullptr),
  _save_custom_path_radio (nullptr),
  _save_group_path_radio (nullptr),
  _articles (articles)
{
  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Save Articles"),
                                                    parent_window,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    nullptr, nullptr);
  gtk_dialog_add_button (GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL);
  GtkWidget * focus = gtk_dialog_add_button (GTK_DIALOG(dialog), "_Save", GTK_RESPONSE_OK);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-save-articles-dialog");
  gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_OK);
  g_signal_connect (dialog, "response", G_CALLBACK(response_cb), this);
  g_signal_connect_swapped (dialog, "destroy", G_CALLBACK(delete_save_dialog), this);

  const std::string group_path (group_prefs.get_string (group, "default-group-save-path", ""));
  const bool have_group_default (!group_path.empty());

  int row (0);
  GtkWidget *t, *w, *h;
  t = HIG :: workarea_create ();

  HIG :: workarea_add_section_spacer (t, row, have_group_default ? 4 : 3);

  if (path_mode==PATH_GROUP && !have_group_default)
      path_mode = PATH_ENTRY;

  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);
  if (have_group_default) {
    w = _save_custom_path_radio = gtk_radio_button_new_with_mnemonic (nullptr, _("_Location:"));
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), path_mode==PATH_ENTRY);
  }
  w = _save_path_entry = file_entry_new (_("Save Articles"));
  gtk_box_pack_start (GTK_BOX(h), w, true, true, 0);
  std::string path (_prefs.get_string ("default-save-attachments-path", ""));

  if (path.empty())
    path = g_get_home_dir ();

  file_entry_set (w, path.c_str());
  g_signal_connect (file_entry_gtk_entry(w), "changed", G_CALLBACK(entry_changed_cb), _save_custom_path_radio);
  gtk_widget_set_size_request (GTK_WIDGET(w), 400, 0);
  w = gtk_button_new_with_label ("Help");
  gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
  g_signal_connect_swapped (w, "clicked", G_CALLBACK (show_group_substitution_help_dialog), dialog);

  if (have_group_default)
    HIG :: workarea_add_wide_control (t, &row, h);
  else
    HIG :: workarea_add_row (t, &row, _("_Location:"), h, file_entry_gtk_entry(_save_path_entry));

  if (have_group_default) {
    char * pch = g_strdup_printf (_("_Group's path: %s"), group_path.c_str());
    w = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON(_save_custom_path_radio), pch);
    _save_group_path_radio = w;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), path_mode==PATH_GROUP);
    g_object_set_data_full (G_OBJECT(w), "default-group-save-path", g_strdup(group_path.c_str()), g_free);
    g_free (pch);
    HIG :: workarea_add_wide_control (t, &row, w);
  }

  w = create_mode_combo_box (prefs);
  g_signal_connect (w,  "changed", G_CALLBACK(mode_combo_box_selection_changed), &prefs);
  w = HIG :: workarea_add_row (t, &row, _("_Action:"), w);

  w = create_priority_combo_box (prefs);
  g_signal_connect (w,  "changed", G_CALLBACK(priority_combo_box_selection_changed), &prefs);
  w = HIG :: workarea_add_row (t, &row, _("_Priority:"), w);

  gtk_widget_show_all (t);
  gtk_box_pack_start (GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG(dialog))), t, true, true, 0);
  gtk_widget_grab_focus (focus);
  _root = dialog;
}
