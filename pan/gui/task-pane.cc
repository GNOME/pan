//* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
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
  #include <glib.h>
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/general/e-util.h>
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include <pan/tasks/queue.h>
#include <pan/icons/pan-pixbufs.h>
#include "pad.h"
#include "render-bytes.h"
#include "task-pane.h"
#include "taskpane.ui.h"

enum
{
  COL_TASK_POINTER,
  COL_TASK_STATE,
  NUM_COLS
};

/**
***  Internal Utility
**/

void
TaskPane :: get_selected_tasks_foreach (GtkTreeModel *model, GtkTreePath *, GtkTreeIter *iter, gpointer list_g)
{
  Task * task (0);
  gtk_tree_model_get (model, iter, COL_TASK_POINTER, &task, -1);
  static_cast<tasks_t*>(list_g)->push_back (task);
}

tasks_t
TaskPane :: get_selected_tasks () const
{
  tasks_t tasks;
  GtkTreeView * view (GTK_TREE_VIEW (_view));
  GtkTreeSelection * sel (gtk_tree_view_get_selection (view));
  gtk_tree_selection_selected_foreach (sel, get_selected_tasks_foreach, &tasks);
  return tasks;
}

/**
***  User Interactions
**/

namespace
{
  int
  find_task_index (GtkListStore * list, Task * task)
  {
    GtkTreeIter iter;
    int index (0);

    if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL(list), &iter)) do
    {
      Task * test;
      gtk_tree_model_get (GTK_TREE_MODEL(list), &iter, COL_TASK_POINTER, &test, -1);
      if (test == task)
        return index;
      ++index;
    }
    while (gtk_tree_model_iter_next (GTK_TREE_MODEL(list), &iter));

    // not found
    return -1;
  }
}

namespace
{
  std::string escaped (const std::string& s)
  {
    char * pch = g_markup_escape_text (s.c_str(), s.size());
    const std::string ret (pch);
    g_free (pch);
    return ret;
  }
}

gboolean
TaskPane:: on_tooltip_query(GtkWidget  *widget,
                            gint        x,
                            gint        y,
                            gboolean    keyboard_tip,
                            GtkTooltip *tooltip,
                            gpointer    data)
{
  TaskPane* tp(static_cast<TaskPane*>(data));

  gtk_tooltip_set_icon_from_stock (tooltip, GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_DIALOG);

  GtkTreeIter iter;
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
  GtkTreePath *path(0);

  char buffer[4096];
  Task * task(0);
  bool task_found(false);

  if (!gtk_tree_view_get_tooltip_context (tree_view, &x, &y, keyboard_tip, &model, &path, &iter))
    return false;

  gtk_tree_model_get (model, &iter, COL_TASK_POINTER, &task, -1);

  g_snprintf(buffer,sizeof(buffer),"...");

  EvolutionDateMaker date_maker;
  char * date(0);

  TaskUpload * tu (dynamic_cast<TaskUpload*>(task));
  if (tu)
  {
    const Article& a(tu->get_article());
    date = date_maker.get_date_string (tu->get_article().time_posted);
    g_snprintf(buffer,sizeof(buffer),
               _("\n<u>Upload</u>\n\n<i>Subject:</i> <b>\"%s\"</b>\n<i>From:</i> <b>%s</b>\n"
                 "<i>Groups:</i> <b>%s</b>\n<i>Sourcefile:</i> <b>%s</b>\n"),
               a.subject.to_string().c_str(), escaped(a.author.to_string()).c_str(),
               tu->get_groups().c_str(), tu->get_filename().c_str());
  }

  TaskArticle * ta (dynamic_cast<TaskArticle*>(task));
  if (ta)
  {
    const Article& a(ta->get_article());
    date = date_maker.get_date_string (ta->get_article().time_posted);
    g_snprintf(buffer,sizeof(buffer),
               _("\n<u>Download</u>\n\n<i>Subject:</i> <b>\"%s\"</b>\n<i>From:</i> <b>%s</b>\n<i>Date:</i> <b>%s</b>\n"
                 "<i>Groups:</i> <b>%s</b>\n<i>Save Path:</i> <b>%s</b>\n"),
               a.subject.to_string().c_str(), escaped(a.author.to_string()).c_str(), date ? date : _("unknown"),
               ta->get_groups().c_str(), ta->get_save_path().to_string().c_str());
  }

  task_found = tu || ta;

  if (task_found)
  {
    gtk_tooltip_set_markup (tooltip, buffer);
    gtk_tree_view_set_tooltip_row (tree_view, tooltip, path);
  }
  gtk_tree_path_free (path);

  g_free (date);

  return true;
}

void
TaskPane::  do_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  TaskPane * self (static_cast<TaskPane*>(userdata));
  GtkWidget * menu (gtk_ui_manager_get_widget (self->_uim, "/taskpane-popup"));
  gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
                  (event ? event->button : 0),
                  (event ? event->time : 0));
}

gboolean
TaskPane :: on_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
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
                                           &path, NULL, NULL, NULL))
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

void TaskPane :: online_toggled_cb (GtkToggleButton* b, Queue *queue)
{
  queue->set_online (gtk_toggle_button_get_active (b));
}
void TaskPane :: up_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.move_up (pane->get_selected_tasks());
}
void TaskPane :: down_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.move_down (pane->get_selected_tasks());
}
void TaskPane :: top_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.move_top (pane->get_selected_tasks());
}
void TaskPane :: bottom_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.move_bottom (pane->get_selected_tasks());
}
void TaskPane :: stop_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.stop_tasks (pane->get_selected_tasks());
}
void TaskPane :: delete_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.remove_tasks (pane->get_selected_tasks());
}
void TaskPane :: restart_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->_queue.restart_tasks (pane->get_selected_tasks());
}

std::string
TaskPane :: prompt_user_for_new_dest (GtkWindow * parent, const Quark& current_path)
{
  char buf[4096];
  struct stat sb;
  std::string path;

  std::string prev_path(current_path.c_str());
  if (!file :: file_exists (prev_path.c_str()))
    prev_path = g_get_home_dir ();

  GtkWidget * w = gtk_file_chooser_dialog_new (_("Choose new destination for selected Tasks"),
                                                GTK_WINDOW(parent),
                                                GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
                                                GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                                GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
                                                NULL);
  gtk_dialog_set_default_response (GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);
  gtk_file_chooser_set_do_overwrite_confirmation (GTK_FILE_CHOOSER (w), TRUE);
  gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (w), prev_path.c_str());

  if (GTK_RESPONSE_ACCEPT == gtk_dialog_run(GTK_DIALOG(w)))
  {
    char * tmp = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER (w));
    path = tmp;
    g_free (tmp);
  } else
    path.clear();

  gtk_widget_destroy (w);
  return path;
}

void
TaskPane :: change_destination (const tasks_t& tasks)
{

  if (tasks.empty()) return;
  TaskArticle * t (dynamic_cast<TaskArticle*>(tasks[0]));
  std::string new_path(prompt_user_for_new_dest(GTK_WINDOW(_root),t->get_save_path()));
  if (new_path.empty()) return; // user cancelled/aborted
  foreach_const (tasks_t, tasks, it) {
    TaskArticle * task (dynamic_cast<TaskArticle*>(*it));
    if (task)
      task->set_save_path(Quark(new_path));
  }

}

void TaskPane :: change_dest_clicked_cb (GtkButton*, TaskPane* pane)
{
  pane->change_destination (pane->get_selected_tasks());
}

/**
***  Display
**/

namespace
{
  typedef Queue::task_states_t task_states_t;
}

/***
****  Queue Listener
***/

void
TaskPane :: on_queue_tasks_added (Queue& queue, int index, int count)
{
  task_states_t states;
  queue.get_all_task_states (states);

  GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(_store));
  g_object_ref(model);
  gtk_tree_view_set_model(GTK_TREE_VIEW(_store), NULL);

  for (int i=0; i<count; ++i)
  {
    const int pos (index + i);
    Task * task (states.tasks[pos]);
    GtkTreeIter iter;
    gtk_list_store_insert (_store, &iter, pos);
    gtk_list_store_set (_store, &iter,
                        COL_TASK_POINTER, task,
                        COL_TASK_STATE, (int)states.get_state(task),
                        -1);
  }
  gtk_tree_view_set_model(GTK_TREE_VIEW(_store), model);
  g_object_unref(model);

}

void
TaskPane :: on_queue_task_removed (Queue&, Task& task, int index)
{
  const int list_index (find_task_index (_store, &task));
  assert (list_index == index);
  GtkTreeIter iter;
  gtk_tree_model_iter_nth_child (GTK_TREE_MODEL(_store), &iter, NULL, index);
  gtk_list_store_remove (_store, &iter);
}

void
TaskPane :: on_queue_task_moved (Queue&, Task&, int new_index, int old_index)
{
  const int count (gtk_tree_model_iter_n_children (GTK_TREE_MODEL(_store), NULL));
  std::vector<int> v (count);
  for (int i=0; i<count; ++i) v[i] = i;
  if (new_index < old_index) {
    v.erase (v.begin()+old_index);
    v.insert (v.begin()+new_index, old_index);
  } else {
    v.erase (v.begin()+old_index);
    v.insert (v.begin()+new_index, old_index);
  }
  gtk_list_store_reorder (_store, &v.front());
}

void TaskPane :: on_queue_task_active_changed (Queue&, Task&, bool) { }
void TaskPane :: on_queue_connection_count_changed (Queue&, int) { }
void TaskPane :: on_queue_size_changed (Queue&, int, int) { }

void TaskPane :: on_queue_online_changed (Queue&, bool online)
{
  g_signal_handler_block (_online_toggle, _online_toggle_handler);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(_online_toggle), online);
  g_signal_handler_unblock (_online_toggle, _online_toggle_handler);

  gtk_image_set_from_stock (GTK_IMAGE(_online_image),
                            online ? GTK_STOCK_CONNECT : GTK_STOCK_DISCONNECT,
                            GTK_ICON_SIZE_BUTTON);
}

void
TaskPane :: update_status (const task_states_t& tasks)
{
  int queued_count (0);
  int stopped_count (0);
  int running_count (0);
  guint64 bytes (0);
  foreach_const (tasks_t, tasks.tasks, it)
  {
    Task * task (*it);
    const Queue::TaskState state (tasks.get_state (task));
    if (state == Queue::RUNNING || state == Queue::DECODING
        || state == Queue::ENCODING)
      ++running_count;
    else if (state == Queue::STOPPED)
      ++stopped_count;
    else if (state == Queue::QUEUED || state == Queue::QUEUED_FOR_DECODE
             || state == Queue::QUEUED_FOR_ENCODE)
      ++queued_count;

    if (state==Queue::RUNNING || state==Queue::QUEUED)
      bytes += task->get_bytes_remaining ();
  }

  // titlebar
  char buf[1024];
  if (stopped_count)
    g_snprintf (buf, sizeof(buf), _("Pan: Tasks (%d Queued, %d Running, %d Stopped)"), queued_count, running_count, stopped_count);
  else if (running_count || queued_count)
    g_snprintf (buf, sizeof(buf), _("Pan: Tasks (%d Queued, %d Running)"), queued_count, running_count);
  else
    g_snprintf (buf, sizeof(buf), _("Pan: Tasks"));
  gtk_window_set_title (GTK_WINDOW(_root), buf);

  // status label
  const unsigned long task_count (running_count + queued_count);
  double KiBps (_queue.get_speed_KiBps ());
  int hours(0), minutes(0), seconds(0);
  if (task_count) {
    const double KiB ((double)bytes / 1024);
    unsigned long tmp (KiBps>0.01 ? ((unsigned long)(KiB / KiBps)) : 0);
    seconds = tmp % 60ul; tmp /= 60ul;
    minutes = tmp % 60ul; tmp /= 60ul;
    hours   = tmp;
  }
  g_snprintf (buf, sizeof(buf), _("%lu tasks, %s, %.1f KiBps, ETA %d:%02d:%02d"),
              task_count, render_bytes(bytes), KiBps, hours, minutes, seconds);
  std::string line (buf);

  const tasks_t tasks_selected (get_selected_tasks ());
  const unsigned long selected_count (tasks_selected.size());
  if (selected_count) {
    guint64 selected_bytes (0ul);
    foreach_const (tasks_t, tasks_selected, it)
      if (*it)
        selected_bytes += (*it)->get_bytes_remaining ();
    g_snprintf (buf, sizeof(buf), _("%lu selected, %s"), selected_count, render_bytes(selected_bytes));
    line += '\n';
    line += buf;
  }

  gtk_label_set_text (GTK_LABEL(_status_label), line.c_str());
}

gboolean
TaskPane :: periodic_refresh_foreach (GtkTreeModel    * model,
                                      GtkTreePath     * ,
                                      GtkTreeIter     * iter,
                                      gpointer          states_gpointer)
{
  Task * task;
  int old_state;
  gtk_tree_model_get (model, iter, COL_TASK_POINTER, &task,
                                   COL_TASK_STATE, &old_state, -1);

  const task_states_t * states (static_cast<const task_states_t*>(states_gpointer));
  const int new_state (states->get_state (task));

  if (new_state != old_state)
    gtk_list_store_set (GTK_LIST_STORE(model), iter, COL_TASK_POINTER, task,
                                                     COL_TASK_STATE, new_state,
                                                     -1);


  return false; // keep iterating
}

gboolean
TaskPane :: periodic_refresh (gpointer pane_gpointer)
{
  TaskPane * pane (static_cast<TaskPane*>(pane_gpointer));
  task_states_t tasks;
  pane->_queue.get_all_task_states (tasks);
  gtk_tree_model_foreach (GTK_TREE_MODEL(pane->_store), periodic_refresh_foreach, &tasks);
  pane->update_status (tasks);
  gtk_widget_queue_draw (pane->_view);

  return true;
}

namespace
{
  void
  render_state (GtkTreeViewColumn *,
                GtkCellRenderer *rend,
                GtkTreeModel *model,
                GtkTreeIter *iter,
                Queue * queue)
  {
    Task * task (0);
    int state;
    gtk_tree_model_get (model, iter, COL_TASK_POINTER, &task,
                                     COL_TASK_STATE,  &state,
                                     -1);

    if (!task) return;

    // build the state string
    const char * state_str (0);
    switch (state) {
      case Queue::RUNNING:  state_str = _("Running"); break;
      case Queue::DECODING: state_str = _("Decoding"); break;
      case Queue::ENCODING: state_str = _("Encoding"); break;
      case Queue::QUEUED_FOR_DECODE: state_str = _("Queued for Decode"); break;
      case Queue::QUEUED_FOR_ENCODE: state_str = _("Queued for Encode"); break;
      case Queue::QUEUED:   state_str = _("Queued"); break;
      case Queue::STOPPED:  state_str = _("Stopped"); break;
      case Queue::REMOVING: state_str = _("Removing"); break;
      default: state_str = _("Unknown"); break;
    }

    // get the time remaining
    const unsigned long bytes_remaining (task->get_bytes_remaining ());
    double speed;
    int connections;
    queue->get_task_speed_KiBps (task, speed, connections);
    const unsigned long seconds_remaining (speed>0.001 ? (unsigned long)(bytes_remaining/(speed*1024)) : 0);
    int h(0), m(0), s(0);
    if (seconds_remaining > 0) {
      h = seconds_remaining / 3600;
      m = (seconds_remaining % 3600) / 60;
      s = seconds_remaining % 60;
    }

    // get the percent done
    const int percent (task->get_progress_of_100());

    // get the description
    const std::string description (task->describe ());

    std::string status (state_str);

    if (percent) {
      char buf[128];
      g_snprintf (buf, sizeof(buf), _("%d%% Done"), percent);
      status += " - ";
      status += buf;
    }
    if (state == Queue::RUNNING) {
      char buf[128];
      g_snprintf (buf, sizeof(buf), _("%d:%02d:%02d Remaining (%d @ %lu KiB/s)"), h, m, s, connections, (unsigned long)speed);
      status += " - ";
      status += buf;
    }

    TaskArticle * ta (dynamic_cast<TaskArticle*>(task));
    if (ta) {
      const Quark& save_path (ta->get_save_path());
      if (!save_path.empty()) {
        status += " - \"";
        status += save_path;
        status += '"';
      }
    }

    if (!queue->is_online()) {
      char buf[512];
      g_snprintf (buf, sizeof(buf), "[%s] ", _("Offline"));
      status.insert (0, buf);
    }

    char * str (0);
    if (state == Queue::RUNNING || state == Queue::DECODING
        || state == Queue::ENCODING)
      str = g_markup_printf_escaped ("<b>%s</b>\n<small>%s</small>", description.c_str(), status.c_str());
    else
      str = g_markup_printf_escaped ("%s\n<small>%s</small>", description.c_str(), status.c_str());
    const std::string str_utf8 = clean_utf8 (str);
    g_object_set(rend, "markup", str_utf8.c_str(), "xpad", 10, "ypad", 5, NULL);
    g_free (str);
  }
}

/**
***
**/

namespace
{
  void delete_task_pane (gpointer pane_gpointer)
  {
    delete static_cast<TaskPane*>(pane_gpointer);
  }
}

TaskPane :: ~TaskPane ()
{
  _queue.remove_listener (this);
  g_source_remove (_update_timeout_tag);
}

namespace
{
  GtkWidget * add_button (GtkWidget   * box,
                                      const gchar * stock_id,
                                      GCallback     callback,
                                      gpointer      user_data)
  {
    GtkWidget * w = gtk_button_new_from_stock (stock_id);
    gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);
    if (callback)
      g_signal_connect (w, "clicked", callback, user_data);
    gtk_box_pack_start (GTK_BOX(box), w, false, false, 0);
    return w;
  }
}

namespace
{

  void do_move_up        (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->up_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_move_down      (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->down_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_move_top       (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->top_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_move_bottom    (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->bottom_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_stop           (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->stop_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_delete         (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->delete_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_restart        (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->restart_clicked_cb(0, static_cast<TaskPane*>(p)); }
  void do_change_dest    (GtkAction*, gpointer p)  { static_cast<TaskPane*>(p)->change_dest_clicked_cb(0, static_cast<TaskPane*>(p)); }

  GtkActionEntry taskpane_popup_entries[] =
  {

    { "move-up", NULL,
      N_("Move Up"), "",
      N_("Move Up"),
      G_CALLBACK(do_move_up) },

    { "move-down", NULL,
      N_("Move Down"), "",
      N_("Move Down"),
      G_CALLBACK(do_move_down) },

    { "move-top", NULL,
      N_("Move To Top"), "",
      N_("Move To Top"),
      G_CALLBACK(do_move_top) },

    { "move-bottom", NULL,
      N_("Move To Bottom"), "",
      N_("Move To Bottom"),
      G_CALLBACK(do_move_bottom) },

    { "stop", NULL,
      N_("Stop Task"), "",
      N_("Stop Task"),
      G_CALLBACK(do_stop) },

    { "delete", "Delete",
      N_("Delete Task"), "Delete",
      N_("Delete Task"),
      G_CALLBACK(do_delete) },

    { "restart", NULL,
      N_("Restart Task"), "",
      N_("Restart Task"),
      G_CALLBACK(do_restart) },

    { "change-dest", NULL,
      N_("Change Download Destination"), "",
      N_("Change Download Destination"),
      G_CALLBACK(do_change_dest) }
  };

}

void
TaskPane :: add_actions (GtkWidget * box)
{
  // action manager for popup
  _uim = gtk_ui_manager_new ();
  // read the file...
  char * filename = g_build_filename (file::get_pan_home().c_str(), "taskpane.ui", NULL);
  GError * err (0);
  if (!gtk_ui_manager_add_ui_from_file (_uim, filename, &err)) {
    g_clear_error (&err);
    gtk_ui_manager_add_ui_from_string (_uim, fallback_taskpane_ui, -1, &err);
  }
  if (err) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), filename, err->message);
    g_clear_error (&err);

  }
  g_free (filename);

//  g_signal_connect (_uim, "add_widget", G_CALLBACK(add_widget), box);

   //add popup actions
  _pgroup = gtk_action_group_new ("taskpane");
  gtk_action_group_set_translation_domain (_pgroup, NULL);
  gtk_action_group_add_actions (_pgroup, taskpane_popup_entries, G_N_ELEMENTS(taskpane_popup_entries), this);
  gtk_ui_manager_insert_action_group (_uim, _pgroup, 0);

}

namespace
{
  gboolean on_popup_menu (GtkWidget * treeview, gpointer userdata)
  {
    TaskPane::do_popup_menu (treeview, NULL, userdata);
    return true;
  }
}

TaskPane :: TaskPane (Queue& queue, Prefs& prefs): _queue(queue)
{
  _root = gtk_window_new (GTK_WINDOW_TOPLEVEL);

  GtkWidget * w;

  GtkWidget * vbox = gtk_vbox_new (false, 0);

    GtkWidget * buttons = gtk_hbox_new (false, PAD_SMALL);

    w = _online_toggle = gtk_check_button_new ();
    _online_toggle_handler = g_signal_connect (w, "clicked", G_CALLBACK(online_toggled_cb), &queue);
    GtkWidget * i = _online_image = gtk_image_new ();
    GtkWidget * l = gtk_label_new_with_mnemonic (_("_Online"));
    GtkWidget * v = gtk_hbox_new (false, PAD);
    gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
    on_queue_online_changed (queue, queue.is_online());
    gtk_box_pack_start (GTK_BOX(v), i, 0, 0, 0);
    gtk_box_pack_start (GTK_BOX(v), l, 0, 0, 0);
    gtk_container_add (GTK_CONTAINER(w), v);
    gtk_box_pack_start (GTK_BOX(buttons), w, false, false, 0);
    gtk_box_pack_start (GTK_BOX(buttons), gtk_vseparator_new(), 0, 0, 0);

    add_button (buttons, GTK_STOCK_GO_UP, G_CALLBACK(up_clicked_cb), this);
    add_button (buttons, GTK_STOCK_GOTO_TOP, G_CALLBACK(top_clicked_cb), this);
    gtk_box_pack_start (GTK_BOX(buttons), gtk_vseparator_new(), 0, 0, 0);
    add_button (buttons, GTK_STOCK_GO_DOWN, G_CALLBACK(down_clicked_cb), this);
    add_button (buttons, GTK_STOCK_GOTO_BOTTOM, G_CALLBACK(bottom_clicked_cb), this);
    gtk_box_pack_start (GTK_BOX(buttons), gtk_vseparator_new(), 0, 0, 0);
    w = add_button (buttons, GTK_STOCK_REDO, G_CALLBACK(restart_clicked_cb), this);
    gtk_widget_set_tooltip_text( w, _("Restart Tasks"));
    w = add_button (buttons, GTK_STOCK_STOP, G_CALLBACK(stop_clicked_cb), this);
    gtk_widget_set_tooltip_text( w, _("Stop Tasks"));
    w = add_button (buttons, GTK_STOCK_DELETE, G_CALLBACK(delete_clicked_cb), this);
    gtk_widget_set_tooltip_text( w, _("Delete Tasks"));
    gtk_box_pack_start (GTK_BOX(buttons), gtk_vseparator_new(), 0, 0, 0);
    w = add_button (buttons, GTK_STOCK_CLOSE, 0, 0);
    g_signal_connect_swapped (w, "clicked", G_CALLBACK(gtk_widget_destroy), _root);
    pan_box_pack_start_defaults (GTK_BOX(buttons), gtk_event_box_new()); // eat horizontal space

  gtk_box_pack_start (GTK_BOX(vbox), buttons, false, false, 0);
  gtk_box_pack_start (GTK_BOX(vbox), gtk_hseparator_new(), false, false, 0);

  // statusbar
  GtkWidget * hbox = gtk_hbox_new (false, PAD);
  w = _status_label = gtk_label_new (0);
  gtk_box_pack_start (GTK_BOX(hbox), w, false, false, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX(vbox), hbox, false, false, PAD);

  _store = gtk_list_store_new (NUM_COLS, G_TYPE_POINTER, G_TYPE_INT);
  _view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(_store));
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(_view), false);
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW(_view), true);
  GtkCellRenderer * renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn * col = gtk_tree_view_column_new_with_attributes (_("State"), renderer, NULL);
  gtk_tree_view_column_set_cell_data_func (col, renderer, (GtkTreeCellDataFunc)render_state, &_queue, 0);
  gtk_tree_view_append_column (GTK_TREE_VIEW(_view), col);
  GtkTreeSelection * selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (_view));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_MULTIPLE);

  // tooltips for articles
  gtk_widget_set_has_tooltip (_view, true);
  g_signal_connect(_view,"query-tooltip",G_CALLBACK(on_tooltip_query), this);

  // connect signals for popup menu
  g_signal_connect (_view, "popup-menu", G_CALLBACK(on_popup_menu), this);
  g_signal_connect (_view, "button-press-event", G_CALLBACK(on_button_pressed), this);

  // actions
  add_actions(_view);
  gtk_window_add_accel_group (GTK_WINDOW(_root), gtk_ui_manager_get_accel_group (_uim));

  w = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), _view);
  gtk_box_pack_start (GTK_BOX(vbox), w, true, true, 0);

  // populate the list
  task_states_t states;
  queue.get_all_task_states (states);
  foreach_r (tasks_t, states.tasks, it) {
    GtkTreeIter iter;
    gtk_list_store_prepend (_store, &iter);
    gtk_list_store_set (_store, &iter,
                        COL_TASK_POINTER, *it,
                        COL_TASK_STATE, (int)states.get_state(*it),
                        -1);
  }

  _queue.add_listener (this);

  _update_timeout_tag = g_timeout_add (750, periodic_refresh, this);

  gtk_window_set_role (GTK_WINDOW(_root), "pan-main-window");
  g_object_set_data_full (G_OBJECT(_root), "pane", this, delete_task_pane);
  gtk_container_add (GTK_CONTAINER(_root), vbox);
  gtk_widget_show_all (vbox);
  prefs.set_window ("tasks-window", GTK_WINDOW(_root), 200, 200, 550, 600);
  gtk_window_set_resizable (GTK_WINDOW(_root), true);
}
