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
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>
#include "progress-view.h"

using namespace pan;

ProgressView :: ProgressView ():
  _progress_step_idle_tag (0),
  _progress_status_idle_tag (0),
  _root (gtk_event_box_new ()),
  _progressbar (gtk_progress_bar_new ()),
  _style (gtk_widget_get_style (_progressbar)),
  _progress (nullptr)
{
  gtk_progress_bar_set_ellipsize (GTK_PROGRESS_BAR(_progressbar), PANGO_ELLIPSIZE_MIDDLE);
  gtk_container_add (GTK_CONTAINER(_root), _progressbar);
  gtk_widget_show (_progressbar);
  gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR(_progressbar), false);
}

ProgressView :: ~ProgressView ()
{
   set_progress(nullptr);
   g_object_unref (_style);
}

void
ProgressView :: set_progress (Progress * progress)
{
  if (progress != _progress)
  {
    if (_progress)
    {
      _progress->remove_listener (this);
      _progress = nullptr;
    }

    if (progress)
    {
      _progress = progress;
      _progress->add_listener (this);
    }
    update_text_soon ();
    update_percentage_soon ();
  }
}

void
ProgressView :: on_progress_error (Progress& p, const StringView& s)
{
  on_progress_status (p, s);
}

gboolean
ProgressView :: on_progress_step_idle (gpointer self_gpointer)
{
  ProgressView * self (static_cast<ProgressView*>(self_gpointer));
  const int of_100 (self->_progress ? self->_progress->get_progress_of_100() : 0);
  gtk_progress_bar_set_fraction (GTK_PROGRESS_BAR(self->_progressbar), of_100/100.0);
  self->_progress_step_idle_tag = 0;
  return false;
}

void
ProgressView :: update_percentage_soon ()
{
  if (!_progress_step_idle_tag)
       _progress_step_idle_tag = g_timeout_add (333, on_progress_step_idle, this);
}
void
ProgressView :: on_progress_step (Progress&, int)
{
  update_percentage_soon ();
}

gboolean
ProgressView :: on_progress_status_idle (gpointer self_gpointer)
{
  ProgressView * self (static_cast<ProgressView*>(self_gpointer));
  std::string status;

  if (self->_progress)
  {
    status  = self->_progress->get_status();
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR(self->_progressbar), true);
  }
  else
    gtk_progress_bar_set_show_text (GTK_PROGRESS_BAR(self->_progressbar), false);
  const char* tmp = iconv_inited ? __g_mime_iconv_strdup(conv,status.c_str()) : nullptr;
  if (tmp) { status = tmp; g_free((char*)tmp); }

  gtk_progress_bar_set_text (GTK_PROGRESS_BAR(self->_progressbar), status.c_str());

  self->_progress_status_idle_tag = 0;

  return false;
}

void
ProgressView :: on_progress_status (Progress&, const StringView&)
{
  update_text_soon ();
}
void
ProgressView :: update_text_soon ()
{
  if (!_progress_status_idle_tag)
       _progress_status_idle_tag = g_timeout_add (333, on_progress_status_idle, this);
}

void ProgressView :: set_color (const std::string& color)
{
  GtkStyle* style = gtk_style_copy (_style);

  gdk_color_parse (color.c_str(), &style->bg[GTK_STATE_PRELIGHT]);
  gtk_widget_set_style (_progressbar, style);
  g_object_unref (style);

}

void ProgressView :: reset_color ()
{
    gtk_widget_set_style (_progressbar, NULL);
}
