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

#include "dl-headers-ui.h"

#include <config.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/macros.h>
#include <pan/tasks/queue.h>
#include <pan/tasks/task-xover.h>
#include "pad.h"
#include "pan/gui/load-gtk-xml.h"


namespace pan {

namespace
{
  struct State
  {
    Data& data;
    Prefs& prefs;
    Queue& queue;
    quarks_t groups;
    GtkWidget * dialog;
    GtkWidget * all_headers_rb;
    GtkWidget * new_headers_rb;
    GtkWidget * n_headers_rb;
    GtkWidget * n_headers_spinbutton;
    GtkWidget * n_days_rb;
    GtkWidget * n_days_spinbutton;

    State (Data& d, Prefs& p, Queue& q):
      data(d),
      prefs(p),
      queue(q),
      dialog(nullptr),
      all_headers_rb(nullptr),
      new_headers_rb(nullptr),
      n_headers_rb(nullptr),
      n_headers_spinbutton(nullptr),
      n_days_rb(nullptr),
      n_days_spinbutton(nullptr)

   {}
  };

  void delete_state (gpointer state_gpointer)
  {
    delete static_cast<State*>(state_gpointer);
  }

  void response_cb (GtkDialog * dialog, int response, gpointer user_data)
  {
    State * state (static_cast<State*>(user_data));

    const int n_headers (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(state->n_headers_spinbutton)));
    const int n_days    (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(state->n_days_spinbutton)));
    state->prefs.set_int ("get-latest-n-headers", n_headers);
    state->prefs.set_int ("get-latest-n-days-headers", n_days);

    if (response == GTK_RESPONSE_ACCEPT)
    {
      const bool mark_read (state->prefs.get_flag ("mark-group-read-before-xover", false));

      foreach_const (quarks_t, state->groups, it) {
        Task * task;
        if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(state->all_headers_rb)))
          task = new TaskXOver (state->data, *it, TaskXOver::ALL);
        else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(state->new_headers_rb)))
          task = new TaskXOver (state->data, *it, TaskXOver::NEW);
        else if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(state->n_headers_rb)))
          task = new TaskXOver (state->data, *it, TaskXOver::SAMPLE, n_headers);
        else // n days
          task = new TaskXOver (state->data, *it, TaskXOver::DAYS, n_days);
        if (mark_read)
          state->data.mark_group_read (*it);
        state->queue.add_task (task, Queue::TOP);
      }
    }

    gtk_widget_destroy (GTK_WIDGET(dialog));
  }

  int
  spin_tickled_cb (GtkWidget *, GdkEventFocus *, gpointer user_data)
  {
    // if a user clicked in the spinbutton window,
    // select the spinbutton radiobutton for them.
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(user_data), true);
    return false;
  }
}

void headers_dialog(Data &data, Prefs &prefs,
                    Queue &queue, const quarks_t &groups, GtkWindow *parent) {
  if (!groups.empty()) {
    std::string title(_("Pan"));
    title += ": ";
    if (groups.size() == 1)
      title += groups.begin()->c_str();
    else {
      char buf[64];
      g_snprintf(buf, sizeof(buf),
                 ngettext("%d Group", "%d Groups", (int)groups.size()),
                 (int)groups.size());
      title += buf;
    }

    State *state = new State(data, prefs, queue);
    state->groups = groups;

    GtkBuilder *builder = gtk_builder_new();
    load_gtk_xml(builder, "dl-headers.ui");

    state->dialog = GTK_WIDGET(gtk_builder_get_object(builder, "main_dialog"));
    state->n_days_rb = GTK_WIDGET(gtk_builder_get_object(builder, "n_days_rb"));
    state->n_days_spinbutton =
        GTK_WIDGET(gtk_builder_get_object(builder, "n_days_spinbutton"));
    state->new_headers_rb =
        GTK_WIDGET(gtk_builder_get_object(builder, "new_headers_rb"));
    state->all_headers_rb =
        GTK_WIDGET(gtk_builder_get_object(builder, "all_headers_rb"));
    state->n_headers_rb =
        GTK_WIDGET(gtk_builder_get_object(builder, "n_headers_rb"));
    state->n_headers_spinbutton =
        GTK_WIDGET(gtk_builder_get_object(builder, "n_headers_spinbutton"));

    // Set values from preferences
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(state->n_days_spinbutton),
                              prefs.get_int("get-latest-n-days-headers", 7));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(state->n_headers_spinbutton),
                              prefs.get_int("get-latest-n-headers", 300));

    // Connect signals
    g_signal_connect(state->n_days_spinbutton, "value_changed",
                     G_CALLBACK(spin_tickled_cb), state->n_days_rb);
    g_signal_connect(state->n_headers_spinbutton, "value_changed",
                     G_CALLBACK(spin_tickled_cb), state->n_headers_rb);
    g_signal_connect(state->dialog, "response", G_CALLBACK(response_cb), state);

    g_object_set_data_full(G_OBJECT(state->dialog), "state", state,
                           delete_state);
    gtk_window_set_title(GTK_WINDOW(state->dialog), title.c_str());
    gtk_window_set_transient_for(GTK_WINDOW(state->dialog), GTK_WINDOW(parent));
    gtk_widget_show_all(state->dialog);

    g_object_unref(builder);
  }
}
} // namespace pan
