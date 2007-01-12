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
#include <pan/general/foreach.h>
#include <pan/icons/pan-pixbufs.h>
#include <pan/tasks/task-article.h>
#include <pan/tasks/queue.h>
#include "hig.h"
#include "pad.h"
#include "pan-file-entry.h"
#include "save-ui.h"

using namespace pan;

namespace
{
  std::string expand_download_dir (const char * dir, const StringView& group)
  {
    std::string val (dir);
    std::string::size_type pos;

    while (((pos = val.find ("%g"))) != val.npos)
      val.replace (pos, 2, group.str, group.len);

    std::string tmp (group.str, group.len);
    std::replace (tmp.begin(), tmp.end(), '.', G_DIR_SEPARATOR);
    while (((pos = val.find ("%G"))) != val.npos)
      val.replace (pos, 2, tmp);

    return val;
  }

  void
  show_group_substitution_help_dialog (gpointer window)
  {
    const char * str = _("%g - group as one directory (alt.binaries.pictures.trains)\n"
                         "%G - group as nested directory (/alt/binaries/pictures/trains)\n"
                         " \n"
                         "\"/home/user/News/Pan/%g\" becomes\n"
                         "\"/home/user/News/Pan/alt.binaries.pictures.trains\", and\n"
                         "\"/home/user/News/Pan/%G\" becomes\n"
                         "\"/home/user/News/Pan/alt/binaries/pictures/trains\",");
    GtkWidget * w = gtk_message_dialog_new (GTK_WINDOW(window),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_INFO,
                                            GTK_BUTTONS_CLOSE, "%s", str);
    g_signal_connect_swapped (GTK_OBJECT(w), "response",
                              G_CALLBACK(gtk_widget_destroy), GTK_OBJECT (w));
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

    // set the path mode based on what widgets exist & are set
    GtkWidget * gr (self->_save_group_path_radio);
    GtkWidget * er (self->_save_custom_path_radio);
    if (gr && er)
      path_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(gr)) ? PATH_GROUP : PATH_ENTRY;
    else
      path_mode = PATH_ENTRY;

    // get the save path
    std::string path;
    if (path_mode == PATH_GROUP)
      path = (const char*) g_object_get_data (G_OBJECT(gr), "default-group-save-path");
    else if (path_mode == PATH_ENTRY)  {
      path = pan :: file_entry_get (self->_save_path_entry);
      self->_prefs.set_string ("default-save-attachments-path", path);
    }
    path = expand_download_dir (path.c_str(), self->_group.to_view());

    // get the save mode
    int save_mode (0);
    const bool save_text (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->_save_text_check)));
    self->_prefs.set_flag ("save-text", save_text);
    if (save_text)
      save_mode |= TaskArticle::RAW;
    const bool save_attachments (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->_save_attachments_check)));
    self->_prefs.set_flag ("save-attachments", save_attachments);
    if (save_attachments)
      save_mode |= TaskArticle::DECODE;

    // make the tasks... 
    Queue::tasks_t tasks;
    foreach_const (std::vector<Article>, self->_articles, it)
      tasks.push_back (new TaskArticle (self->_server_rank,
                                        self->_group_server,
                                        *it,
                                        self->_cache,
                                        self->_read,
                                        0,
                                        TaskArticle::SaveMode(save_mode),
                                        path));

    // get the queue mode...
    Queue::AddMode mode;
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->_position_top_radio)))
      mode = Queue::TOP;
    else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (self->_position_bottom_radio)))
      mode = Queue::BOTTOM;
    else
      mode = Queue::AGE;

    // queue up the tasks...
    if (!tasks.empty())
      self->_queue.add_tasks (tasks, mode);
  }

  gtk_widget_destroy (GTK_WIDGET(dialog));
}

namespace
{
  void entry_changed_cb (GtkEditable * ignored, gpointer radio_or_null)
  {
    if (radio_or_null)
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(radio_or_null), true);
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
  _root (0),
  _save_custom_path_radio (0),
  _save_group_path_radio (0),
  _articles (articles)
{
  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Save Articles"),
                                                    parent_window,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
	                                            GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
	                                            GTK_STOCK_SAVE, GTK_RESPONSE_OK,
                                                    NULL);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-save-articles-dialog");
  gtk_dialog_set_default_response (GTK_DIALOG(dialog), GTK_RESPONSE_OK);
  g_signal_connect (dialog, "response", G_CALLBACK(response_cb), this);
  g_signal_connect_swapped (dialog, "destroy", G_CALLBACK(delete_save_dialog), this);

  const std::string group_path (group_prefs.get_string (group, "default-group-save-path", ""));
  const bool have_group_default (!group_path.empty());

  int row (0);
  GtkWidget *t, *w, *h;
  t = HIG :: workarea_create ();
  HIG::workarea_add_section_title (t, &row, _("Files"));
    HIG :: workarea_add_section_spacer (t, row, 3);

    const bool save_text (_prefs.get_flag ("save-text", false));
    w = HIG :: workarea_add_wide_checkbutton (t, &row, _("Save _Text"), save_text);
    _save_text_check = w;

    const bool save_attachments (_prefs.get_flag ("save-attachments", true));
    w = HIG :: workarea_add_wide_checkbutton (t, &row, _("Save _Attachments"), save_attachments);
    _save_attachments_check = w;

  if (have_group_default)
  {
    HIG::workarea_add_section_divider (t, &row);
    HIG::workarea_add_section_title (t, &row, _("Path"));
      HIG::workarea_add_section_spacer (t, row, 2);
  }

    if (path_mode==PATH_GROUP && !have_group_default)
      path_mode = PATH_ENTRY;

    h = gtk_hbox_new (FALSE, 0);
    if (have_group_default) {
      w = _save_custom_path_radio = gtk_radio_button_new_with_mnemonic (NULL, _("C_ustom path:"));
      gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), path_mode==PATH_ENTRY);
    }
    w = _save_path_entry = file_entry_new (_("Save Files to Path"));
    gtk_box_pack_start (GTK_BOX(h), w, true, true, 0);
    std::string path (_prefs.get_string ("default-save-attachments-path", ""));
    if (path.empty())
      path = g_get_home_dir ();
    file_entry_set (w, path.c_str());
    g_signal_connect (file_entry_gtk_entry(w), "changed", G_CALLBACK(entry_changed_cb), _save_custom_path_radio);
    gtk_widget_set_usize (GTK_WIDGET(w), 400, 0);
    w = gtk_button_new_with_mnemonic (_("_Help"));
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    g_signal_connect_swapped (w, "clicked", G_CALLBACK (show_group_substitution_help_dialog), dialog);
    if (have_group_default)
      HIG :: workarea_add_wide_control (t, &row, h);
    else
      HIG :: workarea_add_row (t, &row, _("_Path:"), h, file_entry_gtk_entry(_save_path_entry));

    if (have_group_default) {
      char * pch = g_strdup_printf (_("Group's _default path: %s"), group_path.c_str());
      w = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON(_save_custom_path_radio), pch);
      _save_group_path_radio = w;
      gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), path_mode==PATH_GROUP);
      g_object_set_data_full (G_OBJECT(w), "default-group-save-path", g_strdup(group_path.c_str()), g_free);
      g_free (pch);
      HIG :: workarea_add_wide_control (t, &row, w);
    }

  HIG::workarea_add_section_divider (t, &row);
  HIG::workarea_add_section_title (t, &row, _("Priority"));
    HIG::workarea_add_section_spacer (t, row, 3);

    // sort by age
    w = _position_age_radio = gtk_radio_button_new_with_mnemonic (NULL, _("Add to the queue sorted by a_ge"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), true);
    HIG :: workarea_add_wide_control (t, &row, w);

    // top of queue
    w = _position_top_radio = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON(w), _("Add to the _front of the queue"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), false);
    HIG :: workarea_add_wide_control (t, &row, w);

    // bottom of queue
    w = _position_bottom_radio = gtk_radio_button_new_with_mnemonic_from_widget (GTK_RADIO_BUTTON(w), _("Add to the _back of the queue"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w), false);
    HIG :: workarea_add_wide_control (t, &row, w);

  gtk_widget_show_all (t);
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(dialog)->vbox), t, true, true, 0);
  _root = dialog;
}
