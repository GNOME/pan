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
  #include <pan/gui/gtk-compat.h>
}
#include <pan/general/string-view.h>
#include "progress-view.h"

using namespace pan;

ProgressView :: ProgressView ():
  _progress_step_idle_tag (0),
  _progress_status_idle_tag (0),
  _root (gtk_event_box_new ()),
  _progressbar (gtk_progress_bar_new ()),
  _progress (0)
{
  gtk_progress_bar_set_ellipsize (GTK_PROGRESS_BAR(_progressbar), PANGO_ELLIPSIZE_MIDDLE);
  gtk_container_add (GTK_CONTAINER(_root), _progressbar);
  gtk_widget_show (_progressbar);
}

void
ProgressView :: set_progress (Progress * progress)
{
  if (progress != _progress)
  {
    if (_progress)
    {
      _progress->remove_listener (this);
      _progress = 0;
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
    status = self->_progress->get_status();
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
