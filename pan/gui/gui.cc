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

#include "pan/gui/load-icon.h"
#include <config.h>
#include <map>
#include <string>
#include <sstream>
#include <fstream>
extern "C" {
  #include <sys/types.h> // for chmod
  #include <sys/stat.h> // for chmod
  #include <dirent.h>
}
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/e-util.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/usenet-utils/scorefile.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/ssl-utils.h>
#include <pan/tasks/task-article.h>
#include <pan/tasks/task-groups.h>
#include <pan/tasks/task-xover.h>
#include <pan/tasks/nzb.h>
#include <pan/data-impl/article-rules.h>
#include "actions.h"
#include "body-pane.h"
#include "dl-headers-ui.h"
#include "editor-spawner.h"
#include "e-charset-dialog.h"
#include "group-pane.h"
#include "group-prefs-dialog.h"
#include "header-pane.h"
#include "hig.h"
#include "license.h"
#include "log-ui.h"
#include "gui.h"
#include "pad.h"

#include "pan.ui.h"

#include "prefs-ui.h"
#include "progress-view.h"
#include "profiles-dialog.h"
#include "post-ui.h"
#include "render-bytes.h"
#include "save-ui.h"
#include "score-add-ui.h"
#include "score-view-ui.h"
#include "server-ui.h"
#include "task-pane.h"
#include "url.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "profiles-dialog.h"

#include <pan/usenet-utils/gpg.h>


namespace
{
  std::string get_accel_filename () {
    char * tmp = g_build_filename (file::get_pan_home().c_str(), "accels.txt", nullptr);
    std::string ret (tmp);
    g_free (tmp);
    return ret;
  }
}

namespace pan
{
  void
  pan_box_pack_start_defaults (GtkBox * box, GtkWidget * child)
  {
    gtk_box_pack_start( box, child, TRUE, TRUE, 0 );
  }

  void remove_from_parent (GtkWidget * w)
  {
    GtkWidget *parent = gtk_widget_get_parent(w);
    if (parent != nullptr)
      gtk_container_remove (GTK_CONTAINER(parent), w);
  }
}

using namespace pan;

namespace
{
  const int VIEW_QTY = 3;

  GtkWindow* get_window (GtkWidget* w)
  {
    return GTK_WINDOW (gtk_widget_get_toplevel (w));
  }

  void
  parent_set_cb (GtkWidget * widget, GtkWidget *, gpointer ui_manager_g)
  {
    GtkWidget * toplevel = gtk_widget_get_toplevel (widget);
    if (GTK_IS_WINDOW (toplevel))
    {
      GtkUIManager * ui_manager = static_cast<GtkUIManager*>(ui_manager_g);
      gtk_window_add_accel_group (GTK_WINDOW(toplevel),
                                  gtk_ui_manager_get_accel_group (ui_manager));
    }
  }

  void set_visible (GtkWidget * w, bool visible)
  {
    if (!visible)
      gtk_widget_hide (w);
    else if (GTK_IS_WINDOW(w))
      gtk_window_present (GTK_WINDOW(w));
    else
      gtk_widget_show (w);
  }

  void toggle_visible (GtkWidget * w)
  {
    set_visible (w, !gtk_widget_get_visible(w));
  }
}

void
GUI :: add_widget (GtkUIManager *,
                   GtkWidget    * widget,
                   gpointer       gui_g)
{
  const char * name (gtk_widget_get_name (widget));
  GUI * self (static_cast<GUI*>(gui_g));

  if (name && strstr(name,"main-window-")==name)
  {
    if (!GTK_IS_TOOLBAR (widget))
    {
      gtk_box_pack_start (GTK_BOX(self->_menu_vbox), widget, FALSE, FALSE, 0);
      gtk_widget_show (widget);
    }
    else
    {
      gtk_toolbar_set_style (GTK_TOOLBAR(widget), GTK_TOOLBAR_ICONS);
      gtk_toolbar_set_icon_size (GTK_TOOLBAR(widget), GTK_ICON_SIZE_SMALL_TOOLBAR);
      gtk_box_pack_start (GTK_BOX(self->_menu_vbox), widget, FALSE, FALSE, 0);
      set_visible (widget, self->is_action_active("show-toolbar"));
      self->_toolbar = widget;
    }
  }
}

void GUI :: show_event_log_cb (GtkWidget *, gpointer gui_gpointer)
{
  static_cast<GUI*>(gui_gpointer)->activate_action ("show-log-dialog");
}

void GUI :: show_task_window_cb (GtkWidget *, gpointer gui_gpointer)
{
  static_cast<GUI*>(gui_gpointer)->activate_action ("show-task-window");
}

void GUI :: show_download_meter_prefs_cb (GtkWidget *, gpointer gui_gpointer)
{
  static_cast<GUI*>(gui_gpointer)->activate_action ("show-dl-meter-prefs");
}

void
GUI :: root_realized_cb (GtkWidget*, gpointer self_gpointer)
{
  GUI* gui (static_cast<GUI*>(self_gpointer));

  StringView last_group = gui->_prefs.get_string("last-visited-group", "");
  if (!last_group.empty())
  {
    gui->_group_pane->read_group(last_group.str);
  }

  // TODO if article is not cached, load with a taskarticle action!
  StringView last_msg = gui->_prefs.get_string("last-opened-msg", "");
  if (!last_msg.empty())
  {
    mid_sequence_t files;
    files.push_back(last_msg);
    GMimeMessage* msg;
#ifdef HAVE_GMIME_CRYPTO
    GPGDecErr err;
    msg = gui->_cache.get_message(files,err);
#else
    msg = gui->_cache.get_message(files);
#endif
    gui->_body_pane->set_text_from_message(msg);
    if (msg)
      g_object_unref(msg);
  }
}

GUI :: GUI (Data& data, Queue& queue, Prefs& prefs, GroupPrefs& group_prefs):
  _data (data),
  _queue (queue),
  _cache (data.get_cache()),
  _encode_cache (data.get_encode_cache()),
  _prefs (prefs),
  _group_prefs (group_prefs),
  _certstore(data.get_certstore()),
  _root (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0)),
  _menu_vbox (gtk_box_new (GTK_ORIENTATION_VERTICAL, 0)),
  _group_pane (nullptr),
  _header_pane (nullptr),
  _body_pane (nullptr),
  _ui_manager (gtk_ui_manager_new ()),
  _info_image (gtk_image_new_from_stock (GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_MENU)),
  _error_image (gtk_image_new_from_stock (GTK_STOCK_DIALOG_ERROR, GTK_ICON_SIZE_MENU)),
  _connection_size_eventbox (nullptr),
  _connection_size_label (nullptr),
  _queue_size_label (nullptr),
  _queue_size_button (nullptr),
  _taskbar (nullptr)
{

  char * filename = g_build_filename (file::get_pan_home().c_str(), "pan.ui", nullptr);
  if (!gtk_ui_manager_add_ui_from_file (_ui_manager, filename, nullptr))
    gtk_ui_manager_add_ui_from_string (_ui_manager, fallback_ui_file, -1, nullptr);
  g_free (filename);
  g_signal_connect (_ui_manager, "add_widget", G_CALLBACK (add_widget), this);
  add_actions (this, _ui_manager, &prefs, &data);
  g_signal_connect (_root, "parent-set", G_CALLBACK(parent_set_cb), _ui_manager);
  gtk_box_pack_start (GTK_BOX(_root), _menu_vbox, FALSE, FALSE, 0);
  gtk_widget_show (_menu_vbox);

  _group_pane = new GroupPane (*this, data, _prefs, _group_prefs);
  _header_pane = new HeaderPane (*this, data, _queue, _cache, _prefs, _group_prefs, *this, *this);
  _body_pane = new BodyPane (data, _cache, _prefs, _group_prefs, _queue, _header_pane);

  std::string path = "/ui/main-window-toolbar";
  GtkWidget * toolbar = gtk_ui_manager_get_widget (_ui_manager, path.c_str());
  path += "/group-pane-filter";
  GtkWidget * w = gtk_ui_manager_get_widget (_ui_manager, path.c_str());
  int index = gtk_toolbar_get_item_index (GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(w));
  GtkToolItem * item = gtk_tool_item_new ();
  gtk_container_add (GTK_CONTAINER(item), _group_pane->create_filter_entry());
  gtk_widget_show_all (GTK_WIDGET(item));
  gtk_toolbar_insert (GTK_TOOLBAR(toolbar), item, index+1);
  path = "/ui/main-window-toolbar/header-pane-filter";
  w = gtk_ui_manager_get_widget (_ui_manager, path.c_str());
  index = gtk_toolbar_get_item_index (GTK_TOOLBAR(toolbar), GTK_TOOL_ITEM(w));
  item = gtk_tool_item_new ();
  gtk_container_add (GTK_CONTAINER(item), _header_pane->create_filter_entry());
  gtk_widget_show_all (GTK_WIDGET(item));
  gtk_toolbar_insert (GTK_TOOLBAR(toolbar), item, index+1);

//  guint merge_id = gtk_ui_manager_new_merge_id (_ui_manager);
//  gtk_ui_manager_add_ui (_ui_manager, merge_id, path, "group-pane-filter", nullptr, GTK_UI_MANAGER_TOOLITEM, true);
//  GtkWidget * item = gtk_ui_manager_get_widget (_ui_manager, path);
//  gtk_container_add (GTK_CONTAINER(item), _group_pane->create_filter_entry());

  // workarea
  _workarea_bin = gtk_event_box_new ();
  do_layout (is_action_active ("tabbed-layout"));
  gtk_box_pack_start (GTK_BOX(_root), _workarea_bin, TRUE, TRUE, 0);
  gtk_widget_show (_workarea_bin);

  /**
  ***  Status Bar
  **/

  w = gtk_event_box_new ();
  gtk_widget_set_size_request (w, -1, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX(_root), w, false, false, 0);
  gtk_widget_show (w);

  GtkWidget * status_bar (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0));

  // connection status
  w = _connection_size_label = gtk_label_new (nullptr);
  gtk_misc_set_padding (GTK_MISC(w), PAD, 0);
  _connection_size_eventbox = gtk_event_box_new ();
  gtk_container_add (GTK_CONTAINER(_connection_size_eventbox), w);
  w = _connection_size_eventbox;
  GtkWidget * frame = gtk_frame_new (nullptr);
  gtk_container_set_border_width (GTK_CONTAINER(frame), 0);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(frame), w);
  gtk_box_pack_start (GTK_BOX(status_bar), frame, FALSE, FALSE, 0);

  // drag and drop for message-ids
  //  gtk_drag_dest_set(_workarea_bin,GTK_DEST_DEFAULT_ALL,target_list,3,GDK_ACTION_COPY);
  //  gtk_drag_dest_add_text_targets(_workarea_bin);
  //  gtk_drag_dest_add_uri_targets(_workarea_bin);

  // queue
  w = _queue_size_label = gtk_label_new (nullptr);
  gtk_misc_set_padding (GTK_MISC(w), PAD, 0);
  w = _queue_size_button = gtk_button_new();
  gtk_widget_set_tooltip_text (w, _("Open the Task Manager"));
  gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);
  g_signal_connect (w, "clicked", G_CALLBACK(show_task_window_cb), this);

  gtk_container_add (GTK_CONTAINER(w), _queue_size_label);
  frame = gtk_frame_new (nullptr);
  gtk_container_set_border_width (GTK_CONTAINER(frame), 0);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(frame), w);
  gtk_box_pack_start (GTK_BOX(status_bar), frame, FALSE, FALSE, 0);

  // status item views
  _taskbar = gtk_table_new (1, VIEW_QTY, TRUE);
  for (int i=0; i<VIEW_QTY; ++i) {
    ProgressView * v = new ProgressView ();
    gtk_table_attach (GTK_TABLE(_taskbar), v->root(), i, i+1, 0, 1, (GtkAttachOptions)~0, (GtkAttachOptions)~0, 0, 0);
    _views.push_back (v);
  }
  gtk_box_pack_start (GTK_BOX(status_bar), _taskbar, true, true, 0);
  gtk_widget_show_all (status_bar);

  // status
  w = _event_log_button = gtk_button_new ();
  gtk_widget_set_tooltip_text (w, _("Open the Event Log"));
  gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);
  gtk_box_pack_start (GTK_BOX(status_bar), w, false, false, 0);
  gtk_container_add (GTK_CONTAINER(w), _info_image);
  g_signal_connect (w, "clicked", G_CALLBACK(show_event_log_cb), this);

  gtk_box_pack_start (GTK_BOX(_root), status_bar, false, false, 0);
  gtk_widget_show_all (status_bar);

  gtk_widget_show (_root);

  upkeep_tag = g_timeout_add (3000, upkeep_timer_cb, this);

  g_object_ref_sink (G_OBJECT(_info_image));
  g_object_ref_sink (G_OBJECT(_error_image));
  g_object_ref (_group_pane->root());
  g_object_ref (_header_pane->root());
  g_object_ref (_body_pane->root());

  do_work_online (is_action_active ("work-online"));

  // update the connections label
  upkeep_timer_cb (this);

  // update the queue label
  int active(0), total(0);
  _queue.get_task_counts (active, total);
  set_queue_size_label (active, total);

  if (_prefs.get_flag ("get-new-headers-on-startup", false))
    activate_action ("get-new-headers-in-subscribed-groups");

  _queue.add_listener (this);
  _prefs.add_listener (this);
  _certstore.add_listener(this);
  Log::get().add_listener (this);
  _data.add_listener (this);

  gtk_accel_map_load (get_accel_filename().c_str());

  { // make sure taskbar views have the right tasks in them --
    // when Pan first starts, the active tasks are already running
    Queue::task_states_t task_states;
    queue.get_all_task_states(task_states);
    foreach(Queue::tasks_t, task_states.tasks, it) {
      Queue::TaskState s = task_states.get_state(*it);
      if (s == Queue::RUNNING || s == Queue::DECODING || s == Queue::ENCODING)
        on_queue_task_active_changed (queue, *(*it), true);
    }
  }

#ifdef HAVE_GMIME_CRYPTO
  init_gpg();
#endif

  g_signal_connect (_root, "realize", G_CALLBACK(root_realized_cb), this);


}

namespace
{
  GtkWidget * hpane (nullptr);
  GtkWidget * vpane (nullptr);
  GtkWidget * sep_vpane (nullptr);
}

GUI :: ~GUI ()
{

  const std::string accel_filename (get_accel_filename());
  gtk_accel_map_save (accel_filename.c_str());
  chmod (accel_filename.c_str(), 0600);

  int res(0);

  if (hpane)
  {
    res = gtk_paned_get_position(GTK_PANED(hpane));
    if (res > 0) _prefs.set_int ("main-window-hpane-position", res);
  }
  if (vpane)
  {
    res = gtk_paned_get_position(GTK_PANED(vpane));
    if (res > 0) _prefs.set_int ("main-window-vpane-position", res);
  }
  if (sep_vpane)
  {
    res = gtk_paned_get_position(GTK_PANED(sep_vpane));
    if (res > 0) _prefs.set_int ("sep-vpane-position", res);
  }


  const bool maximized = gtk_widget_get_window(_root)
                      && (gdk_window_get_state( gtk_widget_get_window(_root)) & GDK_WINDOW_STATE_MAXIMIZED);
  _prefs.set_flag ("main-window-is-maximized", maximized);


  g_source_remove (upkeep_tag);

  std::set<GtkWidget*> unref;
  unref.insert (_body_pane->root());
  unref.insert (_header_pane->root());
  unref.insert (_group_pane->root());
  unref.insert (_error_image);
  unref.insert (_info_image);

  delete _header_pane;
  delete _group_pane;
  delete _body_pane;

  for (size_t i(0), size(_views.size()); i!=size; ++i)
    delete _views[i];

  foreach (std::set<GtkWidget*>, unref, it)
    g_object_unref (*it);
  g_object_unref (G_OBJECT(_ui_manager));
#ifdef HAVE_GMIME_CRYPTO
  deinit_gpg();
#endif
  if (iconv_inited) iconv_close(conv);

  _certstore.remove_listener(this);
  _data.remove_listener(this);
  _prefs.remove_listener (this);
  _queue.remove_listener (this);
  Log::get().remove_listener (this);

}

/***
****
***/

void
GUI :: watch_cursor_on ()
{
  GdkCursor * cursor = gdk_cursor_new (GDK_WATCH);
  gdk_window_set_cursor ( gtk_widget_get_window(_root), cursor);
  g_object_unref (cursor);
}

void
GUI :: watch_cursor_off ()
{
  gdk_window_set_cursor ( gtk_widget_get_window(_root), nullptr);
}

/***
****
***/

namespace
{
  typedef std::map < std::string, GtkAction* > key_to_action_t;

  key_to_action_t key_to_action;

  void ensure_action_map_loaded (GtkUIManager * uim)
  {
    if (!key_to_action.empty())
      return;

    for (GList * l=gtk_ui_manager_get_action_groups(uim); l!=nullptr; l=l->next)
    {
      GtkActionGroup * action_group = GTK_ACTION_GROUP(l->data);
      GList * actions = gtk_action_group_list_actions (action_group);
      for (GList * ait(actions); ait; ait=ait->next) {
        GtkAction * action = GTK_ACTION(ait->data);
        const std::string name (gtk_action_get_name (action));
        key_to_action[name] = action;
      }
      g_list_free (actions);
    }
  }

  GtkAction * get_action (const char * name) {
    key_to_action_t::iterator it = key_to_action.find (name);
    if (it == key_to_action.end()) {
      std::cerr << LINE_ID << " can't find action " << name << std::endl;
      abort ();
    }
    return it->second;
  }
}

bool
GUI :: is_action_active (const char *key) const
{
  ensure_action_map_loaded (_ui_manager);
  return gtk_toggle_action_get_active (GTK_TOGGLE_ACTION(get_action(key)));
}

void
GUI :: activate_action (const char * key) const
{
  ensure_action_map_loaded (_ui_manager);
  gtk_action_activate (get_action(key));
}

void
GUI :: sensitize_action (const char * key, bool b) const
{
  ensure_action_map_loaded (_ui_manager);
  g_object_set (get_action(key), "sensitive", gboolean(b), nullptr);
  //gtk_action_set_sensitive (get_action(key), b);
}

void
GUI :: hide_action (const char * key, bool b) const
{
  ensure_action_map_loaded (_ui_manager);
  g_object_set (get_action(key), "visible", gboolean(!b), nullptr);
  //gtk_action_set_sensitive (get_action(key), b);
}


void
GUI :: toggle_action (const char * key, bool b) const
{
  ensure_action_map_loaded (_ui_manager);
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION(get_action(key)), b);
}

GtkWidget*
GUI :: get_action_widget (const char * key) const
{
  return gtk_ui_manager_get_widget (_ui_manager, key);
}

namespace
{
  gboolean focus_in_cb (GtkWidget     * w,
                        GdkEventFocus * ,
                        gpointer        accel_group_g)
  {
    GtkAccelGroup * accel_group = static_cast<GtkAccelGroup*>(accel_group_g);
    gtk_window_remove_accel_group (get_window(w), accel_group);
    return false;
  }

  gboolean focus_out_cb (GtkWidget     * w,
                         GdkEventFocus * ,
                         gpointer        accel_group_g)
  {
    GtkAccelGroup * accel_group = static_cast<GtkAccelGroup*>(accel_group_g);
    gtk_window_add_accel_group (get_window(w), accel_group);
    return false;
  }
}

void
GUI :: disable_accelerators_when_focused (GtkWidget * w) const
{
  GtkAccelGroup * accel_group = gtk_ui_manager_get_accel_group (_ui_manager);
  g_signal_connect (w, "focus-in-event", G_CALLBACK(focus_in_cb), accel_group);
  g_signal_connect (w, "focus-out-event", G_CALLBACK(focus_out_cb), accel_group);
}

/***
****  PanUI
***/

namespace
{
  static std::string prev_path, prev_file;
}

std::string
GUI :: prompt_user_for_save_path (GtkWindow * parent, const Prefs& prefs)
{
  if (prev_path.empty())
    prev_path = prefs.get_string ("default-save-attachments-path", g_get_home_dir ());
  if (!file :: file_exists (prev_path.c_str()))
    prev_path = g_get_home_dir ();

  GtkWidget * w = gtk_file_chooser_dialog_new (_("Save NZB's Files"), parent,
                                               GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                               nullptr);
  gtk_dialog_set_default_response (GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);
  gtk_file_chooser_select_filename (GTK_FILE_CHOOSER(w), prev_path.c_str());
  const int response (gtk_dialog_run (GTK_DIALOG(w)));
  std::string path;
  if (response == GTK_RESPONSE_ACCEPT) {
    char * tmp = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (w));
    prev_path = path = tmp;
    g_free (tmp);
  }

  gtk_widget_destroy(w);
  return path;
}

std::string
GUI :: prompt_user_for_filename (GtkWindow * parent, const Prefs& prefs)
{

  if (prev_path.empty())
    prev_path = prefs.get_string ("default-save-attachments-path", g_get_home_dir ());
  if (!file :: file_exists (prev_path.c_str()))
  prev_path = g_get_home_dir ();
    prev_file = std::string(_("Untitled.nzb"));

  GtkWidget * w = gtk_file_chooser_dialog_new (_("Save NZB File as..."),
				      parent,
				      GTK_FILE_CHOOSER_ACTION_SAVE,
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				      GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
				      nullptr);
	gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (w), TRUE);
	gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (w), prev_path.c_str());
	gtk_file_chooser_set_current_name (GTK_FILE_CHOOSER (w), prev_file.c_str());

	std::string file;
	const int response (gtk_dialog_run (GTK_DIALOG(w)));
	if (response == GTK_RESPONSE_ACCEPT) {
		char *tmp;
		tmp = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (w));
		file=tmp;
		g_free (tmp);
	}
	gtk_widget_destroy (w);

  return file;
}

void GUI :: do_save_articles ()
{
  std::string path;
  const std::vector<const Article*> articles (_header_pane->get_full_selection_v ());

  std::vector<Article> copies;
  copies.reserve (articles.size());
  foreach_const (std::vector<const Article*>, articles, it)
    copies.push_back (**it);

  if (!copies.empty()) {
    SaveDialog * dialog = new SaveDialog (_prefs, _group_prefs, _data, _data, _cache,
                                          _data, _queue, get_window(_root), _header_pane->get_group(), copies);
    gtk_widget_show (dialog->root());
  }
}

void GUI :: do_read_or_save_articles ()
{
  const guint rows(_header_pane->get_full_selection_rows_num());
  if (rows != 1)
    do_save_articles();
  else
    do_read_selected_article();
}

void GUI :: do_save_articles_to_nzb ()
{

  std::string path;
  const std::vector<const Article*> articles (_header_pane->get_full_selection_v ());

  std::vector<Article> copies;
  copies.reserve (articles.size());
  foreach_const (std::vector<const Article*>, articles, it)
    copies.push_back (**it);

  const bool always (_prefs.get_flag("mark-downloaded-articles-read", false));

  const std::string file (GUI :: prompt_user_for_filename (get_window(_root), _prefs));
    if (!file.empty()) {
      Queue::tasks_t tasks;
      std::string emptystring;
      foreach_const (std::vector<Article>, copies, it)
        tasks.push_back (new TaskArticle (_data, _data, *it, _cache, _data, always ? TaskArticle::ALWAYS_MARK : TaskArticle::NEVER_MARK, nullptr, TaskArticle::RAW,emptystring));

      // write them to a file
      std::ofstream tmp(file.c_str());
      if (tmp.good()) {
        NZB :: nzb_to_xml_file (tmp, tasks);
        tmp.close();
      }
      // cleanup the virtual queue
      foreach(Queue::tasks_t, tasks, it)
        delete *it;
    }
}

namespace
{
  struct SaveArticlesFromNZB: public Progress::Listener
  {
    Data& _data;
    Queue& _queue;
    GtkWidget * _root;
    Prefs& _prefs;
    ArticleCache& _cache;
    EncodeCache & _encode_cache;
    const Article _article;
    const std::string _path;

    SaveArticlesFromNZB (Data& d, Queue& q, GtkWidget *r, Prefs& p,
                         ArticleCache& c, EncodeCache& ec, const Article& a, const std::string& path):
      _data(d), _queue(q), _root(r), _prefs(p), _cache(c), _encode_cache(ec), _article(a), _path(path) {}

    static void foreach_part_cb (GMimeObject */*parent*/, GMimeObject *o, gpointer self)
    {
      static_cast<SaveArticlesFromNZB*>(self)->foreach_part (o);
    }

    void foreach_part (GMimeObject *o)
    {
      if (GMIME_IS_PART(o))
      {
        GMimePart * part (GMIME_PART (o));
        GMimeDataWrapper * wrapper (g_mime_part_get_content (part));
        GMimeStream * mem_stream (g_mime_stream_mem_new ());
        g_mime_data_wrapper_write_to_stream (wrapper, mem_stream);
        const GByteArray * buffer (GMIME_STREAM_MEM(mem_stream)->buffer);
        const StringView nzb ((const char*)buffer->data, buffer->len);
        Queue::tasks_t tasks;
        NZB :: tasks_from_nzb_string (nzb, _path, _cache, _encode_cache, _data, _data, _data, tasks);
        if (!tasks.empty())
          _queue.add_tasks (tasks, Queue::BOTTOM);
        g_object_unref (mem_stream);
      }
    }

    virtual ~SaveArticlesFromNZB() {}

    void on_progress_finished (Progress&, int status) override
    {
      if (status == OK) {
#ifdef HAVE_GMIME_CRYPTO
        GPGDecErr err;
        GMimeMessage * message = _cache.get_message (_article.get_part_mids(), err);
#else
        GMimeMessage * message = _cache.get_message (_article.get_part_mids());
#endif
        g_mime_message_foreach (message, foreach_part_cb, this);
        g_object_unref (message);
      }
      delete this;
    }
  };
}

void GUI :: do_save_articles_from_nzb ()
{
  const bool always (_prefs.get_flag("mark-downloaded-articles-read", false));
  const Article* article (_header_pane->get_first_selected_article ());
  if (article)
  {
    const std::string path (GUI :: prompt_user_for_save_path (get_window(_root), _prefs));
    if (!path.empty())
    {
      SaveArticlesFromNZB * listener = new SaveArticlesFromNZB (_data, _queue, _root,
                                                                _prefs, _cache, _encode_cache, *article, path);
      Task * t = new TaskArticle (_data, _data, *article, _cache, _data, always ? TaskArticle::ALWAYS_MARK : TaskArticle::NEVER_MARK, listener);
      _queue.add_task (t, Queue::TOP);
    }
  }
}

void GUI :: do_print ()
{
  std::cerr << "FIXME " << LINE_ID << std::endl;
}
void GUI :: do_cancel_latest_task ()
{
  _queue.remove_latest_task ();
}
void GUI :: do_import_tasks ()
{
  // get a list of files to import
  GtkWidget * dialog = gtk_file_chooser_dialog_new (
    _("Import NZB Files"), get_window(_root),
    GTK_FILE_CHOOSER_ACTION_OPEN,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
    nullptr);

  GtkFileFilter * filter = gtk_file_filter_new ();
  gtk_file_filter_add_pattern (filter, "*.[Nn][Zz][Bb]");
  gtk_file_filter_set_name (filter, _("NZB Files"));
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

  filter = gtk_file_filter_new ();
  gtk_file_filter_add_pattern (filter, "*");
  gtk_file_filter_set_name (filter, _("All Files"));
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER(dialog), filter);

  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER(dialog), true);
  typedef std::vector<std::string> strings_t;
  strings_t filenames;
  if (gtk_dialog_run (GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    GSList * tmp = gtk_file_chooser_get_filenames (GTK_FILE_CHOOSER(dialog));
    for (GSList * l(tmp); l!=nullptr; l=l->next) {
      filenames.push_back ((char*) l->data);
      g_free (l->data);
    }
    g_slist_free (tmp);
  }
  gtk_widget_destroy (dialog);

  // if we're importing files, build the tasks...
  Queue::tasks_t tasks;
  if (!filenames.empty()) {
    const std::string path (prompt_user_for_save_path (get_window(_root), _prefs));
    if (!path.empty())
      foreach_const (strings_t, filenames, it)
        NZB :: tasks_from_nzb_file (*it, path, _cache, _encode_cache, _data, _data, _data, tasks);
  }

  if (!tasks.empty())
    _queue.add_tasks (tasks, Queue::BOTTOM);
}

void GUI :: do_import_tasks_from_nzb_stream (const char* stream)
{
  // if we're importing files, build the tasks...
  Queue::tasks_t tasks;
  const std::string path (prompt_user_for_save_path (get_window(_root), _prefs));
  if (!path.empty())
    NZB :: tasks_from_nzb_string(stream, path, _cache, _encode_cache, _data, _data, _data, tasks);

  if (!tasks.empty())
    _queue.add_tasks (tasks, Queue::BOTTOM);
}

namespace
{
  void task_pane_destroyed_cb (GtkWidget *, gpointer p)
  {
    TaskPane ** task_pane (static_cast<TaskPane**>(p));
    *task_pane = nullptr;
  }
}

void GUI :: do_show_task_window ()
{
  static TaskPane * task_pane (nullptr);

  if (!task_pane) {
    task_pane = new TaskPane (_queue, _prefs);
    g_signal_connect (task_pane->root(), "destroy",
                      G_CALLBACK(task_pane_destroyed_cb), &task_pane);
  }

  toggle_visible (task_pane->root());
}

namespace
{
  void set_bin_child (GtkWidget * w, GtkWidget * new_child)
  {
    GtkWidget * child (gtk_bin_get_child (GTK_BIN(w)));
    if (child != new_child)
    {
      gtk_container_remove (GTK_CONTAINER(w), child);
      gtk_container_add (GTK_CONTAINER(w), new_child);
      gtk_widget_show (new_child);
    }
  }
}

void GUI :: on_log_entry_added (const Log::Entry& e)
{
  if (e.severity & Log::PAN_SEVERITY_ERROR)
    set_bin_child (_event_log_button, _error_image);

  if (_queue.is_online() && (e.severity & Log::PAN_SEVERITY_URGENT)) {
    GtkWidget * w = gtk_message_dialog_new (get_window(_root),
                                            GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
                                            GTK_MESSAGE_ERROR,
                                            GTK_BUTTONS_CLOSE,
                                            "%s", e.message.c_str());
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show_all (w);
  }
}

void GUI :: do_show_log_window ()
{
  set_bin_child (_event_log_button, _info_image);

  static GtkWidget * w (nullptr);
  if (!w) {
    w = log_dialog_new (_prefs, get_window (_root));
    g_signal_connect (w, "destroy", G_CALLBACK(gtk_widget_destroyed), &w);
  }

  toggle_visible (w);
}
void GUI :: do_select_all_articles ()
{
  _header_pane->select_all ();
}
void GUI :: do_unselect_all_articles ()
{
  _header_pane->unselect_all ();
}
void GUI :: do_add_threads_to_selection ()
{
  _header_pane->select_threads ();
}
void GUI :: do_add_similar_to_selection ()
{
  _header_pane->select_similar ();
}
void GUI :: do_add_subthreads_to_selection ()
{
  _header_pane->select_subthreads ();
}
void GUI :: do_select_article_body ()
{
  _body_pane->select_all ();
}
void GUI :: do_show_preferences_dialog ()
{
  PrefsDialog * dialog = new PrefsDialog (_prefs, get_window(_root));
  g_signal_connect (dialog->root(), "destroy", G_CALLBACK(prefs_dialog_destroyed_cb), this);
  gtk_widget_set_size_request(dialog->root(), 800, 600);
  gtk_widget_show (dialog->root());
}
void GUI :: do_show_group_preferences_dialog ()
{
  quarks_v groups(_group_pane->get_full_selection());
  if (!groups.empty()) {
    GroupPrefsDialog * dialog = new GroupPrefsDialog (_data, groups, _prefs, _group_prefs, get_window(_root));
    gtk_widget_show (dialog->root());
  }
}

void GUI :: do_show_profiles_dialog ()
{
  ProfilesDialog d (_data, _data, get_window(_root));
  gtk_dialog_run (GTK_DIALOG(d.root()));
  gtk_widget_destroy (d.root());
}

void GUI :: do_jump_to_group_tab ()
{
  toggle_action ("tabbed-layout", true);
  GtkNotebook * n (GTK_NOTEBOOK (gtk_bin_get_child( GTK_BIN (_workarea_bin))));
  gtk_notebook_set_current_page (n, 0);
}

void GUI :: do_jump_to_header_tab ()
{
  toggle_action ("tabbed-layout", true);
  GtkNotebook * n (GTK_NOTEBOOK (gtk_bin_get_child( GTK_BIN (_workarea_bin))));
  gtk_notebook_set_current_page (n, 1);
}

void GUI :: do_jump_to_body_tab ()
{
  toggle_action ("tabbed-layout", true);
  GtkNotebook * n (GTK_NOTEBOOK (gtk_bin_get_child( GTK_BIN (_workarea_bin))));
  gtk_notebook_set_current_page (n, 2);
}

void GUI :: do_rot13_selected_text ()
{
  _body_pane->rot13_selected_text ();
}


void GUI :: on_progress_finished (Progress & p, int status)
{
  TaskArticle * ta = dynamic_cast<TaskArticle*>(&p);
  if (status==OK && ta)
  {
    _body_pane->set_article (ta->get_article());

    // maybe update the tab
    if (is_action_active ("tabbed-layout"))
      activate_action ("jump-to-body-tab");
  }
}

void GUI :: do_read_selected_article ()
{
  const Article* article (_header_pane->get_first_selected_article ());
  if (article)
  {
    const bool expand (_prefs.get_flag("expand-selected-articles",false));
    const bool always (_prefs.get_flag("mark-downloaded-articles-read", false));

    if (expand) _header_pane->expand_selected ();

    Task * t = new TaskArticle (_data, _data, *article, _cache, _data, always ? TaskArticle::ALWAYS_MARK : TaskArticle::NEVER_MARK, this);
    _queue.add_task (t, Queue::TOP);
  }
}
void GUI :: do_download_selected_article ()
{
  typedef std::vector<const Article*> article_vector_t;
  const article_vector_t articles (_header_pane->get_full_selection_v ());
  const bool always (_prefs.get_flag("mark-downloaded-articles-read", false));
  Queue::tasks_t tasks;
  foreach_const (article_vector_t, articles, it)
    tasks.push_back (new TaskArticle (_data, _data, **it, _cache, _data, always ? TaskArticle::ALWAYS_MARK : TaskArticle::NEVER_MARK));
  if (!tasks.empty())
    _queue.add_tasks (tasks, Queue::TOP);
}
void GUI :: do_clear_header_pane ()
{
  gtk_window_set_title (get_window(_root), _("Pan"));
  _header_pane->clear ();
}

void GUI :: do_clear_body_pane ()
{
  _body_pane->clear ();
}
void GUI :: do_read_more ()
{
  if (!_body_pane->read_more ())
  {
    activate_action (_prefs.get_flag ("space-selects-next-article", true)
                     ? "read-next-article"
                     : "read-next-unread-article");
  }
}

void GUI :: do_read_less ()
{
  if (!_body_pane->read_less ())
    activate_action ("read-previous-article");
}
void GUI :: do_read_next_unread_group ()
{
  _group_pane->read_next_unread_group ();
}
void GUI :: do_read_next_group ()
{
  _group_pane->read_next_group ();
}
void GUI :: do_read_next_unread_article ()
{
  _header_pane->read_next_unread_article ();
}
void GUI :: do_read_next_article ()
{
  _header_pane->read_next_article ();
}
void GUI :: do_read_next_watched_article ()
{
  std::cerr << "FIXME " << LINE_ID << std::endl;
}
void GUI :: do_read_next_unread_thread ()
{
  std::cerr << "FIXME " << LINE_ID << std::endl;
  _header_pane->read_next_unread_thread ();
}
void GUI :: do_read_next_thread ()
{
  _header_pane->read_next_thread ();
}
void GUI :: do_read_previous_article ()
{
  _header_pane->read_previous_article ();
}
void GUI :: do_read_previous_thread ()
{
  _header_pane->read_previous_thread ();
}
void GUI :: do_read_parent_article ()
{
  _header_pane->read_parent_article ();
}

void GUI ::  server_list_dialog_destroyed_cb (GtkWidget * w, gpointer self)
{
  static_cast<GUI*>(self)->server_list_dialog_destroyed (w);
}

void GUI ::  sec_dialog_destroyed_cb (GtkWidget * w, gpointer self)
{
  static_cast<GUI*>(self)->sec_dialog_destroyed (w);
}


// this queues up a grouplist task for any servers that
// were added while the server list dialog was up.
void GUI :: server_list_dialog_destroyed (GtkWidget *)
{
  quarks_t all_servers (_data.get_servers());
  foreach_const (quarks_t, all_servers, it) {
    quarks_t tmp;
    _data.server_get_groups (*it, tmp);
    if (tmp.empty() && _data.get_server_limits(*it))
      _queue.add_task (new TaskGroups (_data, *it));
  }
}


void GUI :: sec_dialog_destroyed (GtkWidget * w)
{
  // NOTE : unused for now
}

void GUI ::  prefs_dialog_destroyed_cb (GtkWidget * w, gpointer self)
{
  static_cast<GUI*>(self)->prefs_dialog_destroyed (w);
}

void GUI :: prefs_dialog_destroyed (GtkWidget *)
{

  const Quark& group (_header_pane->get_group());
  if (!group.empty() && _prefs._rules_changed)
  {
    _prefs._rules_changed = !_prefs._rules_changed;
    _header_pane->rules(_prefs._rules_enabled);
  }
  _cache.set_max_megs(_prefs.get_int("cache-size-megs",10));

}


void GUI :: do_show_servers_dialog ()
{
  GtkWidget * w = server_list_dialog_new (_data, _queue, _prefs, get_window(_root));
  gtk_widget_show_all (w);
  g_signal_connect (w, "destroy", G_CALLBACK(server_list_dialog_destroyed_cb), this);
}


void GUI :: do_show_sec_dialog ()
{
#ifdef HAVE_GNUTLS
  GtkWidget * w = sec_dialog_new (_data, _queue, _prefs, get_window(_root));
  g_signal_connect (w, "destroy", G_CALLBACK(sec_dialog_destroyed_cb), this);
  gtk_widget_show_all (w);
#endif
}

void GUI :: do_collapse_thread ()
{
  _header_pane->collapse_selected();
}

void GUI :: do_expand_thread ()
{
  _header_pane->expand_selected();
}


void GUI :: do_show_score_dialog ()
{
  const Quark& group (_header_pane->get_group());
  const Article * article (nullptr);
  if (!group.empty())
    article = _header_pane->get_first_selected_article ();
  if (article) {
    GtkWindow * window (get_window (_root));
    ScoreView * view = new ScoreView (_data, window, group, *article);
    gtk_widget_show (view->root());
  }
}

void GUI :: set_selected_thread_score (int score)
{
  const Article* article (_header_pane->get_first_selected_article ());
  // If no article is selected
  if (!article)
    return;

  Quark group (_header_pane->get_group());
  std::string references;
  _data.get_article_references (group, article, references);
  StringView v(references), tok;
  v.pop_token (tok);
  if (tok.empty())
    tok = article->message_id.c_str();

  // if this is the article or a descendant...
  Scorefile::AddItem items[2];
  items[0].on = true;
  items[0].negate = false;
  items[0].key = "Message-ID";
  items[0].value = TextMatch::create_regex (tok, TextMatch::IS);
  items[1].on = true;
  items[1].negate = false;
  items[1].key = "References";
  items[1].value = TextMatch::create_regex (tok, TextMatch::CONTAINS);

  _data.add_score (StringView(group), score, true, 31, false, items, 2, true);
}
void GUI :: do_watch ()
{
  set_selected_thread_score (9999);
}
void GUI :: do_ignore ()
{
  set_selected_thread_score (-9999);
}

void
GUI :: do_flag ()
{
  std::vector<const Article*> v(_header_pane->get_full_selection_v());
  if (v.empty()) return;

  foreach (std::vector<const Article*>,v,it)
  {
    Article* a((Article*)*it);
    a->set_flag(!a->get_flag());
  }
  const Quark& g(_header_pane->get_group());
  _data.fire_article_flag_changed(v, g);
}

void
GUI :: do_flag_off ()
{
  std::vector<const Article*> v(_header_pane->get_full_selection_v());
  if (v.empty()) return;

  foreach (std::vector<const Article*>,v,it)
  {
    Article* a((Article*)*it);
    a->set_flag(false);
  }
  const Quark& g(_header_pane->get_group());
  _data.fire_article_flag_changed(v, g);
}


void
GUI :: do_mark_all_flagged()
{
  _header_pane->mark_all_flagged();
}

void
GUI :: step_bookmarks(int step)
{
  _header_pane->move_to_next_bookmark(step);
}

void
GUI :: do_next_flag ()
{
  step_bookmarks(1);
}

void
GUI :: do_last_flag ()
{
  step_bookmarks(-1);
}

void
GUI :: do_invert_selection ()
{
  _header_pane->invert_selection();
}

void GUI :: do_plonk ()
{
  score_add (ScoreAddDialog::PLONK);
}
void GUI :: do_show_new_score_dialog ()
{
  score_add (ScoreAddDialog::ADD);
}
void
GUI :: score_add (int mode)
{
  Quark group (_header_pane->get_group());
  if (group.empty())
    group = _group_pane->get_first_selection();

  Article a;
  const Article* article (_header_pane->get_first_selected_article ());
  if (article != nullptr)
    a = *article;

  ScoreAddDialog * d = new ScoreAddDialog (_data, _root, group, a, (ScoreAddDialog::Mode)mode);
  gtk_widget_show (d->root());
}

void GUI :: do_supersede_article ()
{
  GMimeMessage * message (_body_pane->get_message ());
  if (!message)
    return;

  // did this user post the message?
  InternetAddressList * Lsender (g_mime_message_get_sender (message));
  char * sender = internet_address_list_to_string(Lsender, nullptr,  TRUE);
  const bool user_posted_this (_data.has_from_header (sender));

  if (!user_posted_this) {
    GtkWidget * w = gtk_message_dialog_new (
      get_window(_root),
      GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, nullptr);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(w),
      _("Unable to supersede article."),
      _("The article doesn't match any of your posting profiles."));
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show (w);
    g_object_unref (message);
    return;
  }

  // copy the body and preserve the essential headers...
  // if we copy the entire message, then we get all the
  // headers tacked on by the news server.
  const char * cpch;
  char * old_mid (g_strdup_printf ("<%s>", g_mime_message_get_message_id(message)));
  GMimeMessage * new_message (g_mime_message_new (false));
  GMimeObject * new_message_obj = (GMimeObject*)new_message;

  g_mime_object_set_header (new_message_obj, "Supersedes", old_mid, nullptr);
  const char * addr = internet_address_list_to_string (g_mime_message_get_sender (message), nullptr, TRUE);
  g_mime_message_add_mailbox (new_message, GMIME_ADDRESS_TYPE_SENDER, nullptr, addr);
  g_mime_message_set_subject (new_message, g_mime_message_get_subject (message), nullptr);
  g_mime_object_set_header (new_message_obj, "Newsgroups", g_mime_object_get_header ((GMimeObject *)message, "Newsgroups"), nullptr);
  g_mime_object_set_header (new_message_obj, "References", g_mime_object_get_header ((GMimeObject *)message, "References"), nullptr);
  const char * r_addr = internet_address_list_to_string(g_mime_message_get_reply_to (message), nullptr, TRUE);
  if ((cpch = r_addr))
              g_mime_message_add_mailbox (new_message, GMIME_ADDRESS_TYPE_REPLY_TO, nullptr, cpch);
  if ((cpch = g_mime_object_get_header ((GMimeObject *)message,     "Followup-To")))
              g_mime_object_set_header (new_message_obj, "Followup-To", cpch, nullptr);
  gboolean  unused (false);
  char * body (pan_g_mime_message_get_body (message, &unused));
  GMimeStream * stream = g_mime_stream_mem_new_with_buffer (body, strlen(body));
  g_free (body);
  GMimeDataWrapper * content_object = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_DEFAULT);
  GMimePart * part = g_mime_part_new ();
   g_mime_part_set_content(part, content_object);
  g_mime_message_set_mime_part (new_message, GMIME_OBJECT(part));
  g_object_unref (part);
  g_object_unref (content_object);
  g_object_unref (stream);

  PostUI * post = PostUI :: create_window (nullptr, _data, _queue, _data, _data, new_message, _prefs, _group_prefs, _encode_cache);
  if (post)
  {
    gtk_widget_show_all (post->root());

    GtkWidget * w = gtk_message_dialog_new (
      GTK_WINDOW(post->root()),
      GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
      GTK_MESSAGE_INFO,
      GTK_BUTTONS_CLOSE, nullptr);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(w),
      _("Revise and send this article to replace the old one."),
      _("Be patient!  It will take time for your changes to take effect."));
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show (w);
  }

  g_object_unref (message);
  g_free (old_mid);
}

void GUI :: do_cancel_article ()
{
  GMimeMessage * message (_body_pane->get_message ());
  if (!message)
    return;

  // did this user post the message?
  const char * sender = internet_address_list_to_string(g_mime_message_get_sender (message), nullptr, TRUE);
  const bool user_posted_this (_data.has_from_header (sender));
  if (!user_posted_this) {
    GtkWidget * w = gtk_message_dialog_new (
      get_window(_root),
      GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE, nullptr);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(w),
      _("Unable to cancel article."),
      _("The article doesn't match any of your posting profiles."));
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show (w);
    g_object_unref (message);
    return;
  }

  // okay then...
  GMimeMessage * cancel = g_mime_message_new (false);
  char * cancel_message = g_strdup_printf ("cancel <%s>", g_mime_message_get_message_id(message));
  const char * sk_addr = internet_address_list_to_string(g_mime_message_get_sender (message), nullptr, TRUE);
  g_mime_message_add_mailbox (cancel, GMIME_ADDRESS_TYPE_SENDER, nullptr, sk_addr);
  g_mime_message_set_subject (cancel, "Cancel", nullptr);
  g_mime_object_set_header ((GMimeObject *)cancel, "Newsgroups", g_mime_object_get_header ((GMimeObject *)message, "Newsgroups"), nullptr);
  g_mime_object_set_header ((GMimeObject *)cancel, "Control", cancel_message, nullptr);
  const char * body ("Ignore\r\nArticle canceled by author using " PACKAGE_STRING "\r\n");
  GMimeStream * stream = g_mime_stream_mem_new_with_buffer (body, strlen(body));
  GMimeDataWrapper * content_object = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_DEFAULT);
  GMimePart * part = g_mime_part_new ();
  g_mime_part_set_content (part, content_object);
  g_mime_message_set_mime_part (cancel, GMIME_OBJECT(part));
  g_object_unref (part);
  g_object_unref (content_object);
  g_object_unref (stream);
  g_free (cancel_message);

  PostUI * post = PostUI :: create_window (nullptr, _data, _queue, _data, _data, cancel, _prefs, _group_prefs, _encode_cache);
  if (post)
  {
    gtk_widget_show_all (post->root());

    GtkWidget * w = gtk_message_dialog_new (
      GTK_WINDOW(post->root()),
      GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
      GTK_MESSAGE_INFO,
      GTK_BUTTONS_CLOSE, nullptr);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(w),
      _("Send this article to ask your server to cancel your other one."),
      _("Be patient!  It will take time for your changes to take effect."));
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show (w);
  }

  g_object_unref (message);
}

bool GUI::deletion_confirmation_dialog() const
{
  bool ret(false);
  GtkWidget * d = gtk_message_dialog_new (
    get_window(_root),
    GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
    GTK_MESSAGE_WARNING,
    GTK_BUTTONS_NONE, nullptr);
  HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
    _("You have marked some articles for deletion."),
    _("Are you sure you want to delete them?"));
  gtk_dialog_add_buttons (GTK_DIALOG(d),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
                          GTK_STOCK_APPLY, GTK_RESPONSE_YES,
                          nullptr);
  gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_NO);
  ret = gtk_dialog_run (GTK_DIALOG(d)) == GTK_RESPONSE_YES;
  gtk_widget_destroy(d);
  return ret;
}

#ifdef HAVE_GNUTLS
bool GUI :: confirm_accept_new_cert_dialog(GtkWindow * parent, gnutls_x509_crt_t cert, const Quark& server)
{

  char buf[4096*256];
  std::string host; int port;
  _data.get_server_addr(server,host,port);
  pretty_print_x509(buf,sizeof(buf), host, cert, true);
  GtkWidget * d = gtk_message_dialog_new (
    parent,
    GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
    GTK_MESSAGE_WARNING,
    GTK_BUTTONS_NONE, nullptr);

  HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d), buf,
    _("Do you want to accept it permanently? (You can change this later.)"));
  gtk_dialog_add_buttons (GTK_DIALOG(d),
                          GTK_STOCK_CANCEL, GTK_RESPONSE_NO,
                          GTK_STOCK_APPLY, GTK_RESPONSE_YES,
                          nullptr);
  gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_NO);

  gint ret_code = gtk_dialog_run (GTK_DIALOG(d));

  if (ret_code == GTK_RESPONSE_YES)
  {
    debug_SSL("set server trust to enabled");
    _data.set_server_trust (server, 1);
    _data.save_server_info(server);
  }

  gtk_widget_destroy(d);

  return ret_code == GTK_RESPONSE_YES;
}
#endif

void GUI :: do_delete_article ()
{
  bool confirm (_prefs.get_flag("show-deletion-confirm-dialog", true));
  bool do_delete(false);
  if (confirm)
  {
    if (deletion_confirmation_dialog())
      do_delete = true;
  } else
    do_delete = true;

  if (do_delete)
  {
    const std::set<const Article*> articles (_header_pane->get_nested_selection(false));
    _data.delete_articles (articles);

    const Quark mid (_body_pane->get_message_id());
    foreach_const (std::set<const Article*>, articles, it)
      if ((*it)->message_id == mid)
        _body_pane->clear ();
  }
}

void GUI :: do_clear_article_cache ()
{
  _cache.clear ();
  _encode_cache.clear();
}

void GUI :: do_mark_article_read ()
{
  const std::set<const Article*> article_set (_header_pane->get_nested_selection (false));
  const std::vector<const Article*> tmp (article_set.begin(), article_set.end());
  _data.mark_read ((const Article**)&tmp.front(), tmp.size());
}

void GUI :: do_mark_article_unread ()
{
  const std::set<const Article*> article_set (_header_pane->get_nested_selection (false));
  const std::vector<const Article*> tmp (article_set.begin(), article_set.end());
  _data.mark_read ((const Article**)&tmp.front(), tmp.size(), false);
}

void GUI :: do_mark_thread_read ()
{
  const std::set<const Article*> article_set (_header_pane->get_nested_selection (true));
  const std::vector<const Article*> tmp (article_set.begin(), article_set.end());
  _data.mark_read ((const Article**)&tmp.front(), tmp.size());
}

void GUI :: do_mark_thread_unread ()
{
  const std::set<const Article*> article_set (_header_pane->get_nested_selection (true));
  const std::vector<const Article*> tmp (article_set.begin(), article_set.end());
  _data.mark_read ((const Article**)&tmp.front(), tmp.size(), false);
}

void
GUI :: do_post ()
{
  GMimeMessage * message = g_mime_message_new (false);

  // add newsgroup...
  std::string newsgroups;
  const quarks_v groups (_group_pane->get_full_selection ());
  foreach_const (quarks_v, groups, it) {
    newsgroups += *it;
    newsgroups += ',';
  }
  if (!newsgroups.empty())
    newsgroups.resize (newsgroups.size()-1); // erase trailing comma
  if (newsgroups.empty()) {
    const Quark group (_header_pane->get_group());
    if (!group.empty())
      newsgroups = group;
  }
  if (!newsgroups.empty())
    g_mime_object_append_header ((GMimeObject *) message, "Newsgroups", newsgroups.c_str(), nullptr);

  // content type
  GMimePart * part = g_mime_part_new ();
  GMimeContentType *type = g_mime_content_type_parse (nullptr, "text/plain; charset=UTF-8");
  g_mime_object_set_content_type ((GMimeObject *) part, type);
  g_object_unref (type);
  g_mime_part_set_content_encoding (part, GMIME_CONTENT_ENCODING_8BIT);
  g_mime_message_set_mime_part (message, GMIME_OBJECT(part));
  g_object_unref (part);

  PostUI * post = PostUI :: create_window (nullptr, _data, _queue, _data, _data, message, _prefs, _group_prefs, _encode_cache);
  if (post)
    gtk_widget_show_all (post->root());
  g_object_unref (message);
}

void GUI :: do_followup_to ()
{
  GMimeMessage * message = _body_pane->create_followup_or_reply (false);
  if (message) {
    PostUI * post = PostUI :: create_window(nullptr, _data, _queue, _data, _data, message, _prefs, _group_prefs, _encode_cache);
    if (post)
      gtk_widget_show_all (post->root());
    g_object_unref (message);
  }
}
void GUI :: do_reply_to ()
{
  GMimeMessage * message = _body_pane->create_followup_or_reply (true);
  if (message) {
    PostUI * post = PostUI :: create_window (nullptr, _data, _queue, _data, _data, message, _prefs, _group_prefs, _encode_cache);
    if (post)
      gtk_widget_show_all (post->root());
    g_object_unref (message);
  }
}
#ifdef HAVE_MANUAL
void GUI :: do_pan_manual ()
{
  GError * error (nullptr);
  gtk_show_uri_on_window (nullptr, "help:pan", gtk_get_current_event_time (), &error);
    if (error) {
      GtkWidget * w = gtk_message_dialog_new (get_window(_root),
                                              GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
                                              GTK_MESSAGE_ERROR,
                                              GTK_BUTTONS_CLOSE,
                                              _("Unable to open help file."));
      g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
      gtk_widget_show_all (w);
  }
}
#endif
void GUI :: do_pan_web ()
{
  URL :: open (_prefs, "https://gitlab.gnome.org/GNOME/pan/-/blob/master/README.org");
}
void GUI :: do_bug_report ()
{
  URL :: open (_prefs, "https://gitlab.gnome.org/GNOME/pan/issues");
}
void GUI :: do_about_pan ()
{
  const gchar * authors [] = { "Charles Kerr <charles@rebelbase.com> - Pan Author",
                               "Heinrich M\u00fcller <heinrich.mueller82@gmail.com> - Developer",
                               "Kenneth Haley <haleykd@users.sf.net> - Developer",
                               "Petr Kovar <pknbe@volny.cz> - Contributor",
                               "Calin Culianu <calin@ajvar.org> - Threaded Decoding",
                               "Christophe Lambin <chris@rebelbase.com> - Original Developer",
                               "Matt Eagleson <matt@rebelbase.com> - Original Developer", nullptr };
  GdkPixbuf * logo = load_icon("icon_pan_about_logo.png");
  GtkAboutDialog * w (GTK_ABOUT_DIALOG (gtk_about_dialog_new ()));
  gtk_about_dialog_set_program_name (w, _("Pan"));
  gtk_about_dialog_set_version (w, PACKAGE_VERSION);
#ifdef PLATFORM_INFO
#ifdef GIT_REV
  gtk_about_dialog_set_comments (w, VERSION_TITLE " (" GIT_REV "; " PLATFORM_INFO ")");
#else
  gtk_about_dialog_set_comments (w, VERSION_TITLE " (" PLATFORM_INFO ")");
#endif // GIT_REV
#else
  gtk_about_dialog_set_comments (w, VERSION_TITLE);
#endif // PLATFORM_INFO

  gtk_about_dialog_set_copyright (w, _("Copyright \u00A9 2002-2021 Charles Kerr and others")); // \u00A9 is unicode for (c)
  gtk_about_dialog_set_website (w, "https://gitlab.gnome.org/GNOME/pan/-/blob/master/README.org");
  gtk_about_dialog_set_logo (w, logo);
  gtk_about_dialog_set_license (w, LICENSE);
  gtk_about_dialog_set_authors (w, authors);
  gtk_about_dialog_set_translator_credits (w, _("translator-credits"));
  g_signal_connect (G_OBJECT (w), "response", G_CALLBACK (gtk_widget_destroy), nullptr);
  gtk_widget_show_all (GTK_WIDGET(w));
  g_object_unref (logo);
}

void GUI :: do_work_online (bool b)
{
  _queue.set_online (b);
  refresh_connection_label ();
}

namespace
{
  enum { HORIZONTAL, VERTICAL };

  void hpane_destroy_cb (GtkWidget *, gpointer)
  {
    std::cerr << LINE_ID << std::endl;
  }
  void vpane_destroy_cb (GtkWidget *, gpointer)
  {
    std::cerr << LINE_ID << std::endl;
  }

  GtkWidget* pack_widgets (Prefs& prefs, GtkWidget * w1, GtkWidget * w2, int orient, gint uglyhack_idx)
  {

    GtkWidget * w;
    if (w1!=nullptr && w2!=nullptr) {
      int pos(0);
      if (uglyhack_idx==0)
      {
        pos = prefs.get_int ("main-window-vpane-position", 300);
      }
      else if (uglyhack_idx==1)
      {
        pos = prefs.get_int ("main-window-hpane-position", 266);
      }
      else if (uglyhack_idx==2)
      {
        pos = prefs.get_int ("sep-vpane-position", 266);
      }
      if (orient == VERTICAL) {
        if (uglyhack_idx==0)
        {
          w = vpane = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
        }
        else if (uglyhack_idx==2)
        {
          w = sep_vpane = gtk_paned_new (GTK_ORIENTATION_VERTICAL);
        }
        gtk_widget_set_size_request (w1, -1, 50);
        gtk_widget_set_size_request (w2, -1, 50);
      } else {
        w = hpane = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_size_request (w1, 50, -1);
        gtk_widget_set_size_request (w2, 50, -1);
      }
      gtk_paned_pack1 (GTK_PANED(w), w1, false, true);
      gtk_paned_pack2 (GTK_PANED(w), w2, true, false);
      gtk_paned_set_position (GTK_PANED(w), pos);
    }
    else if (w1!=nullptr)
      w = w1;
    else if (w2!=nullptr)
      w = w2;
    else
      w = nullptr;
    return w;
  }

  gboolean grab_focus_idle (gpointer w)
  {
    gtk_widget_grab_focus (GTK_WIDGET(w));
    return false;
  }
}

void
GUI :: notebook_page_switched_cb (GtkNotebook *, void *, gint page_num, gpointer user_data)
{
  GUI * gui = static_cast<GUI*>(user_data);
  GtkWidget * w;
  switch (page_num) {
    case 0: w = gui->_group_pane->get_default_focus_widget(); break;
    case 1: w = gui->_header_pane->get_default_focus_widget(); break;
    case 2: w = gui->_body_pane->get_default_focus_widget(); break;
    default: g_assert (0 && "invalid page reached!"); break;
  }
  g_idle_add (grab_focus_idle, w);
}

//TODO
void GUI :: do_layout (bool tabbed)
{
  if (hpane) {
    _prefs.set_int ("main-window-hpane-position", gtk_paned_get_position(GTK_PANED(hpane)));
    hpane = nullptr;
  }
  if (vpane) {
    _prefs.set_int ("main-window-vpane-position", gtk_paned_get_position(GTK_PANED(vpane)));
    vpane = nullptr;
  }

  gtk_widget_hide (_workarea_bin);

  GtkWidget * group_w (_group_pane->root());
  GtkWidget * header_w (_header_pane->root());
  GtkWidget * search_w;
  GtkWidget * body_w (_body_pane->root());

  remove_from_parent (group_w);
  remove_from_parent (header_w);
  remove_from_parent (body_w);

  // remove workarea's current child
  GList * children = gtk_container_get_children (GTK_CONTAINER(_workarea_bin));
  if (children) {
    gtk_container_remove (GTK_CONTAINER(_workarea_bin), GTK_WIDGET(children->data));
    g_list_free (children);
  }

  GtkWidget * w (nullptr);
  if (tabbed)
  {
    w = gtk_notebook_new ();
    GtkNotebook * n (GTK_NOTEBOOK (w));
    gtk_notebook_append_page (n, group_w, gtk_label_new_with_mnemonic (_("_1. Group Pane")));
    gtk_notebook_append_page (n, header_w, gtk_label_new_with_mnemonic (_("_2. Header Pane")));
    gtk_notebook_append_page (n, body_w, gtk_label_new_with_mnemonic (_("_3. Body Pane")));
    g_signal_connect (n, "switch-page", G_CALLBACK(notebook_page_switched_cb), this);
  }
  else
  {
    GtkWidget *p[3];
    const std::string layout (_prefs.get_string ("pane-layout", "stacked-right"));
    const std::string orient (_prefs.get_string ("pane-orient", "groups,headers,body"));

    StringView tok, v(orient);
    v.pop_token(tok,','); if (*tok.str=='g') p[0]=group_w; else if (*tok.str=='h') p[0]=header_w; else p[0]=body_w;
    v.pop_token(tok,','); if (*tok.str=='g') p[1]=group_w; else if (*tok.str=='h') p[1]=header_w; else p[1]=body_w;
    v.pop_token(tok,','); if (*tok.str=='g') p[2]=group_w; else if (*tok.str=='h') p[2]=header_w; else p[2]=body_w;

    if (layout == "stacked-top") {
      w = pack_widgets (_prefs, p[0], p[1], HORIZONTAL, 1);
      w = pack_widgets (_prefs, w, p[2], VERTICAL, 0);
    } else if (layout == "stacked-bottom") {
      w = pack_widgets (_prefs, p[1], p[2], HORIZONTAL, 1);
      w = pack_widgets (_prefs, p[0], w, VERTICAL, 0);
    } else if (layout == "stacked-left") {
      w = pack_widgets (_prefs, p[0], p[1], VERTICAL, 0);
      w = pack_widgets (_prefs, w, p[2], HORIZONTAL, 1);
    } else if (layout == "stacked-vertical") {
      w = pack_widgets (_prefs, p[0], p[1], VERTICAL, 0);
      w = pack_widgets (_prefs, w, p[2], VERTICAL, 2);
    } else { // stacked right
      w = pack_widgets (_prefs, p[1], p[2], VERTICAL, 0);
      w = pack_widgets (_prefs, p[0], w,    HORIZONTAL, 1);
    }
  }

  gtk_container_add (GTK_CONTAINER(_workarea_bin), w);
  gtk_widget_show_all (_workarea_bin);
  set_visible (group_w, is_action_active("show-group-pane"));
  set_visible (header_w, is_action_active("show-header-pane"));
  set_visible (body_w, is_action_active("show-body-pane"));

  if (tabbed)
    gtk_notebook_set_current_page (GTK_NOTEBOOK( gtk_bin_get_child( GTK_BIN(_workarea_bin))), 0);
}

void GUI :: do_show_group_pane (bool b)
{
  set_visible (_group_pane->root(), b);
}
void GUI :: do_show_header_pane (bool b)
{
  set_visible (_header_pane->root(), b);
}
void GUI :: do_show_body_pane (bool b)
{
  set_visible (_body_pane->root(), b);
}
void GUI :: do_show_toolbar (bool b)
{
  set_visible (_toolbar, b);
}
void GUI :: do_shorten_group_names (bool b)
{
  _group_pane->set_name_collapse (b);
}

void GUI :: do_match_only_read_articles   (bool) { _header_pane->refilter (); }
void GUI :: do_match_only_unread_articles (bool) { _header_pane->refilter (); }
void GUI :: do_match_only_cached_articles (bool) { _header_pane->refilter (); }
void GUI :: do_match_only_binary_articles (bool) { _header_pane->refilter (); }
void GUI :: do_match_only_my_articles     (bool) { _header_pane->refilter (); }
void GUI :: do_match_on_score_state       (int)  { _header_pane->refilter (); }
void GUI :: do_enable_toggle_rules        (bool enable) { _header_pane -> rules (enable); }

void GUI :: do_show_matches (const Data::ShowType show_type)
{
  _header_pane->set_show_type (show_type);
}

namespace
{
  std::string bytes_to_size(unsigned long val)
  {
    int i(0);
    double d(val);
    while (d >= 1024.0) { d /= 1024.0; ++i; }
    std::stringstream out;
    out << d;
    std::string ret(out.str());

    switch (i)
    {
      case 0:
        ret += _(" Bytes");
        break;
      case 1:
        ret += _(" KB");
        break;
      case 2:
        ret += _(" MB");
        break;
      case 3:
        ret += _(" GB");
        break;
      case 4:
        ret += _(" TB");
        break;
      default:
        ret += _(" Bytes");
        break;
    }
    return ret;
  }
}

void GUI :: do_show_selected_article_info ()
{
  const Article* a = _header_pane->get_first_selected_article ();

  if (a != nullptr)
  {
    // date
    EvolutionDateMaker date_maker;
    char * date = date_maker.get_date_string (a->time_posted);

    // article parts
    typedef Parts::number_t number_t;
    std::set<number_t> missing_parts;
    const number_t n_parts = a->get_total_part_count ();
    for (number_t i=1; i<=n_parts; ++i)
      missing_parts.insert (i);
    for (Article::part_iterator it(a->pbegin()), e(a->pend()); it!=e; ++it)
      missing_parts.erase (it.number());
    char msg[512];
    *msg = '\0';
    std::ostringstream s;
    if (missing_parts.empty())
      g_snprintf (msg, sizeof(msg), ngettext("This article is complete with %d part.","This article has all %d parts.",(int)n_parts), (int)n_parts);
    else
      g_snprintf (msg, sizeof(msg), ngettext("This article is missing %d part.","This article is missing %d of its %d parts:",(int)n_parts), (int)missing_parts.size(), (int)n_parts);
    foreach_const (std::set<number_t>, missing_parts, it)
      s << ' ' << *it;

    const char* author = iconv_inited ? __g_mime_iconv_strdup(conv, a->author.c_str()) : a->author.c_str();
    const char* subject = iconv_inited ? __g_mime_iconv_strdup(conv, a->subject.c_str()) : a->subject.c_str();

    GtkWidget * w = gtk_message_dialog_new_with_markup (
      get_window(_root),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_INFO,
      GTK_BUTTONS_CLOSE,
        "<b>%s</b>: %s\n" "<b>%s</b>: %s\n"
        "<b>%s</b>: %s\n" "<b>%s</b>: %s\n"
        "<b>%s</b>: %lu\n" "<b>%s</b>: %s (%lu %s)\n"
        "\n"
        "%s" "%s",
        _("Subject"), subject, _("From"), author,
        _("Date"), date, _("Message-ID"), a->message_id.c_str(),
        _("Lines"), a->get_line_count(), _("Size"), bytes_to_size(a->get_byte_count()).c_str(),
        a->get_byte_count(),_("Bytes"),
        msg, s.str().c_str());
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show_all (w);

    // cleanup
    g_free (date);
    if (iconv_inited)
    {
      g_free ((char*)author);
      g_free ((char*)subject);
    }

  }
}

void GUI :: do_quit ()
{
  gtk_main_quit ();
}

void GUI :: do_read_selected_group ()
{
  const Quark group (_group_pane->get_first_selection ());

  // update the titlebar
  std::string s (_("Pan"));
  if (!group.empty()) {
    s += ": ";
    s += group.c_str();
  }
  gtk_window_set_title (get_window(_root), s.c_str());

  // set the charset encoding based upon that group's / global default
  if (!group.empty())
  {
    std::string local (_group_prefs.get_string (group, "character-encoding", "UTF-8"));
    set_charset (local);

    // update iconv handler
    const char * from = g_mime_charset_iconv_name(local.c_str());
    char buf[256];
    g_snprintf(buf, sizeof(buf), "%s//IGNORE", _prefs.get_string("default-charset", "UTF-8").str);
    const char * to  = g_mime_charset_iconv_name(buf);
    {
      if (iconv_inited)
        iconv_close(conv);
      conv = iconv_open (to, from);
      if (conv == (iconv_t)-1)
      {
        Log::add_err(_("Error loading iconv library. Encoding certain character sets will not work in GUI."));
      } else
        iconv_inited = true;
    }

  }


  // update the header pane
  watch_cursor_on ();
  const Quark old_group(_header_pane->get_group());
  const bool changed  (old_group != group);
  if (changed)
  {
    _header_pane->set_group (group);
    _header_pane->set_focus ();
  }
  watch_cursor_off ();

  // periodically save our state
  _data.save_state ();
  _prefs.save ();
  _group_prefs.save ();

  // maybe update the tab
  if (is_action_active ("tabbed-layout"))
    activate_action ("jump-to-header-tab");

  // update the body pane
  activate_action ("clear-body-pane");

  // if it's the first time in this group, pop up a download-headers dialog.
  // otherwise if get-new-headers is turned on, queue an xover-new task.
  Article_Count unread(0), total(0);

  if (changed && !group.empty()){
    _data.get_group_counts (group, unread, total);
    const bool virtual_group (GroupPane::is_virtual_group(group));
    if (!virtual_group)
    {
      if (static_cast<uint64_t>(total) == 0)
        activate_action ("download-headers");
      else if (_prefs.get_flag("get-new-headers-when-entering-group", true)) {
        if (_prefs.get_flag ("mark-group-read-before-xover", false))
          _data.mark_group_read (group);
        _queue.add_task (new TaskXOver (_data, group, TaskXOver::NEW), Queue::TOP);
      }
    }
  }

  // fire group_entered for status icon
  if (changed)
  {
    _data.get_group_counts (group, unread, total);
    _data.fire_group_entered (group, unread, total);
  }
}

void GUI :: do_mark_selected_groups_read ()
{
  const quarks_v group_names (_group_pane->get_full_selection ());
  foreach_const (quarks_v, group_names, it)
    _data.mark_group_read (*it);
}

void GUI :: do_clear_selected_groups ()
{
  bool confirm (_prefs.get_flag("show-deletion-confirm-dialog", true));
  bool do_delete(false);
  if (confirm)
  {
    if (deletion_confirmation_dialog())
      do_delete = true;
  } else
    do_delete = true;

  if (do_delete)
  {
      const quarks_v groups (_group_pane->get_full_selection ());
      foreach_const (quarks_v, groups, it)
      _data.group_clear_articles (*it);
  }
}

void GUI :: do_xover_selected_groups ()
{
  const quarks_v groups (_group_pane->get_full_selection ());
  const bool mark_read (_prefs.get_flag ("mark-group-read-before-xover", false));
  foreach_const (quarks_v, groups, it) {
    if (mark_read)
      _data.mark_group_read (*it);
    _queue.add_task (new TaskXOver (_data, *it, TaskXOver::NEW), Queue::TOP);
  }
}

void GUI :: do_xover_subscribed_groups ()
{
  typedef std::vector<Quark> quarks_v;
  quarks_v groups;
  _data.get_subscribed_groups (groups);
  const bool mark_read (_prefs.get_flag ("mark-group-read-before-xover", false));
  foreach_const_r (quarks_v, groups, it) {
    if (mark_read)
      _data.mark_group_read (*it);
    _queue.add_task (new TaskXOver (_data, *it, TaskXOver::NEW), Queue::TOP);
  }
}

void GUI :: do_download_headers ()
{
  const quarks_v groups_v (_group_pane->get_full_selection ());
  const quarks_t groups (groups_v.begin(), groups_v.end());
  headers_dialog (_data, _prefs, _queue, groups, get_window(_root));
}

void GUI :: do_refresh_groups ()
{
  Queue::tasks_t tasks;

  const quarks_t servers (_data.get_servers ());
  foreach_const_r (quarks_t, servers, it)
    if (_data.get_server_limits(*it))
      tasks.push_back (new TaskGroups (_data, *it));

  if (!tasks.empty())
    _queue.add_tasks (tasks);
}

void GUI :: do_subscribe_selected_groups ()
{
  const quarks_v group_names (_group_pane->get_full_selection ());
  foreach_const (quarks_v, group_names, it)
    _data.set_group_subscribed (*it, true);
}
void GUI :: do_unsubscribe_selected_groups ()
{
  const quarks_v groups (_group_pane->get_full_selection ());
  foreach_const (quarks_v, groups, it)
    _data.set_group_subscribed (*it, false);
}
void GUI :: do_prompt_for_charset ()
{
  if (_charset.empty())
      _charset = "UTF-8";

  char * tmp = e_charset_dialog (_("Character Encoding"),
                                 _("Body Pane Encoding"),
                                 _charset.c_str(),
                                 get_window(root()));
  set_charset (tmp);
  free (tmp);
}


void GUI :: set_charset (const StringView& s)
{
  _charset.assign (s.str, s.len);
  _body_pane->set_character_encoding (_charset.c_str());
}

/***
****
***/

void
GUI :: refresh_connection_label ()
{
  char str[128];
  char tip[4096];

  const double KiBps = _queue.get_speed_KiBps ();
  int active, idle, connecting;
  _queue.get_connection_counts (active, idle, connecting);

  // decide what to say
  if (!_queue.is_online())
  {
    g_snprintf (str, sizeof(str), _("Offline"));

    int num(active+idle);
    if (active || idle)
        g_snprintf (tip, sizeof(tip), ngettext("Closing %d connection","Closing %d connections", num), num);
    else
      g_snprintf (tip, sizeof(tip), _("No Connections"));
  }
  else if (!active && !idle && connecting)
  {
    g_snprintf (str, sizeof(str), _("Connecting"));
    g_snprintf (tip, sizeof(tip), "%s", str);
  }
  else if (active || idle)
  {
    typedef std::vector<Queue::ServerConnectionCounts> counts_t;
    counts_t counts;
    _queue.get_full_connection_counts (counts);
    int port;
    std::string s, addr;
    foreach_const (counts_t, counts, it) {
      _data.get_server_addr (it->server_id, addr, port);
      char buf[1024];
      g_snprintf (buf, sizeof(buf), _("%s: %d idle, %d active @ %.1f KiBps"),
                  addr.c_str(), it->idle, it->active, it->KiBps);
      s += buf;
      s += '\n';
    }
    if (!s.empty())
      s.resize (s.size()-1); // get rid of trailing linefeed

    g_snprintf (str, sizeof(str), "%d @ %.1f KiB/s", active, KiBps);
    g_snprintf (tip, sizeof(tip), "%s", s.c_str());
  }
  else
  {
    g_snprintf (str, sizeof(str), _("No Connections"));
    g_snprintf (tip, sizeof(tip), "%s", str);
  }

  gtk_label_set_text (GTK_LABEL(_connection_size_label), str);
  gtk_widget_set_tooltip_text (_connection_size_eventbox, tip);
}

namespace
{
  void
  timeval_diff (GTimeVal * start, GTimeVal * end, GTimeVal * diff)
  {
    diff->tv_sec = end->tv_sec - start->tv_sec;
    if (end->tv_usec < start->tv_usec)
      diff->tv_usec = 1000000ul + end->tv_usec - start->tv_usec, --diff->tv_sec;
    else
      diff->tv_usec = end->tv_usec - start->tv_usec;
  }
}

int
GUI :: upkeep_timer_cb (gpointer gui_g)
{
  static_cast<GUI*>(gui_g)->upkeep ();
  return true;
}

void
GUI :: upkeep ()
{
  refresh_connection_label ();
}

void
GUI :: set_queue_size_label (unsigned int running,
                             unsigned int size)
{
  char str[256];
  char tip[256];

  // build the button label
  if (!size)
    g_snprintf (str, sizeof(str), _("No Tasks"));
  else
    g_snprintf (str, sizeof(str), "%s: %u/%u", _("Tasks"), running, size);

  // build the tooltip
  // FIX : build fix for 64bit osx which doesn't seem to have guint64t nor gulong :(
#ifdef G_OS_DARWIN
  unsigned long queued, unused, stopped;
  uint64_t KiB_remain;
#else
  gulong queued, unused, stopped;
  guint64 KiB_remain;
#endif
  double KiBps;
  int hr, min, sec;
  _queue.get_stats (queued, unused, stopped,
                    KiB_remain, KiBps,
                    hr, min, sec);

  g_snprintf (tip, sizeof(tip), _("%lu tasks, %s, %.1f KiBps, ETA %d:%02d:%02d"),
              (running+queued), render_bytes(KiB_remain), KiBps, hr, min, sec);

  // update the gui
  gtk_label_set_text (GTK_LABEL(_queue_size_label), str);
  gtk_widget_set_tooltip_text (_queue_size_button, tip);
}

void
GUI :: on_queue_task_active_changed (Queue&, Task& t, bool is_active)
{
  // update our set of active tasks
  std::list<Task*>::iterator it (std::find (_active_tasks.begin(), _active_tasks.end(), &t));
  if (is_active && it==_active_tasks.end())
    _active_tasks.push_back (&t);
  else if (!is_active && it!=_active_tasks.end())
    _active_tasks.erase (it);

  // update all the views
  int i (0);
  it = _active_tasks.begin();
  for (; it!=_active_tasks.end() && i<VIEW_QTY; ++it, ++i)
    _views[i]->set_progress (*it);
  for (; i<VIEW_QTY; ++i)
    _views[i]->set_progress (nullptr);
}
void
GUI :: on_queue_connection_count_changed (Queue&, int)
{
  //connection_size = count;
  refresh_connection_label ();
}
void
GUI :: on_queue_size_changed (Queue&, int active, int total)
{
  set_queue_size_label (active, total);
}
void
GUI :: on_queue_online_changed (Queue&, bool is_online)
{
  toggle_action ("work-online", is_online);
}
void
GUI :: on_queue_error (Queue&, const StringView& message)
{
  if (_queue.is_online())
  {
    std::string s;
    if (!message.empty()) {
      s.assign (message.str, message.len);
      s += "\n \n";
    }
    s += _("Pan is now offline. Please see \"File|Event Log\" and correct the problem, then use \"File|Work Online\" to continue.");
    Log::add_urgent_va ("%s", s.c_str());

    toggle_action ("work-online", false);
  }
}


void
GUI :: on_prefs_flag_changed (const StringView& key, bool val)
{
  if (key == "show-taskpane-popups")
  {
    _prefs.save();
  }
}


void
GUI :: on_prefs_string_changed (const StringView& key, const StringView& value)
{
  if (key == "pane-layout" || key == "pane-orient")
    GUI :: do_layout (_prefs.get_flag ("tabbed-layout", false));

  if (key == "default-save-attachments-path")
    prev_path.assign (value.str, value.len);

  if (key == "cache-file-extension")
  {
    _prefs.save();
    StringView tmp(value);
    // default to "eml" if value is empty to conform to article-cache
    if (tmp.empty()) tmp ="eml";
    _data.get_cache().set_msg_extension(tmp);
  }
}

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
GUI :: do_edit_scores (GtkAction *act)
{
  //Protect against bouncy keypresses
  if (not gtk_action_get_sensitive(act)) {
    return;
  }

  char *filename{g_strdup(_data.get_scorefile_name().c_str())};
  Destroyer d{filename};
  if (not file::file_exists(filename)) {
    FILE *f = fopen(filename, "a+");
    if (f == nullptr) {
      Log::add_err_va("Error creating file '%s'", filename);
      return;
    }
    fclose(f);
  }

  try
  {
    using namespace std::placeholders;
    _spawner.reset(
      new EditorSpawner(filename,
                        std::bind(&GUI::edit_scores_cleanup, this, _1, _2, act),
                        _prefs));
    d.retain();
    gtk_action_set_sensitive(act, false);
  }
  catch (EditorSpawnerError const &)
  {
    //There should be a big red exclamation on the status line
  }
}

void
GUI :: edit_scores_cleanup(int status, char *filename, GtkAction *act)
{
  g_free(filename);
  _data.rescore();
  gtk_action_set_sensitive(act, true);
  _spawner.reset();
  gtk_window_present(get_window(_root));
}

#ifdef HAVE_GNUTLS

void
GUI :: do_show_cert_failed_dialog(VerifyData* data)
{
  pan_debug("do show cert failed dialog");
  VerifyData& d(*data);
  bool delete_cert = false;
  if (GUI::confirm_accept_new_cert_dialog(get_window(_root),d.cert,d.server))
  {
    if (!_certstore.add(d.cert, d.server))
    {
      Log::add_urgent_va("Error adding certificate of server '%s' to Certificate Store",d.server.c_str());
      delete_cert = true;
    }
  }

  if (delete_cert) d.deinit_cert();
  delete data;
}

gboolean
GUI :: show_cert_failed_cb(gpointer gp)
{
  pan_debug("show_cert_failed_cb");
  VerifyData* d(static_cast<VerifyData*>(gp));
  d->gui->do_show_cert_failed_dialog(d);
  return false;
}

void
GUI :: on_verify_cert_failed(gnutls_x509_crt_t cert, std::string server, int nr)
{
  pan_debug("on verify failed GUI ("<<cert<<") ("<<server<<")");
  if (!cert || server.empty()) return;

  VerifyData* data = new VerifyData();
  data->cert = cert;
  data->server = server;
  data->nr = nr;
  data->gui = this;
  g_idle_add(show_cert_failed_cb, data);
}

void
GUI :: on_valid_cert_added (gnutls_x509_crt_t cert, std::string server)
{
  /* whitelist to make avaible for nntp-pool */
  _certstore.whitelist(server);
  debug_SSL("whitelist ("<<server<<") ("<<cert<<")");
}


#endif
