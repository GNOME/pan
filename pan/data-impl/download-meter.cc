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
#include <pan/data-impl/download-meter.h>
#include <pan/data-impl/data-impl.h>
#include <iostream>
#include <math.h>

extern "C" {
  #include <glib/gi18n.h>
}

using namespace pan;


DownloadMeterImpl :: ~DownloadMeterImpl()
{
}


DownloadMeterImpl :: DownloadMeterImpl (Prefs& prefs, Data& data) :
                _view(new ProgressView()),
                _progress(new Progress("Downloaded bytes")),
                _prefs(prefs),
                _data(data),
                _downloaded_bytes(0ul),
                _limit(1ul)
{

  dl_meter_update ();

  _view->set_progress(_progress);
  _progress->init_steps(100);
  _progress->set_status(_("DL Init ...."));

  GtkWidget* w = _button = gtk_button_new();
  gtk_widget_set_tooltip_text (w, _("Open Download Meter Preferences"));
  gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);

  gtk_container_add (GTK_CONTAINER(w), _view->root());
  GtkWidget* frame = _widget = gtk_frame_new (NULL);
  gtk_container_set_border_width (GTK_CONTAINER(frame), 0);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(frame), w);

  gtk_widget_set_visible(_widget, prefs.get_flag("dl-meter-show", true));

}

namespace
{

  std::string mnemonic(int cnt)
  {
    std::string ret;

    switch (cnt)
    {
      default:
      case 0:
        ret = _("Bytes");
        break;
      case 1:
        ret = _("KB");
        break;
      case 2:
        ret = _("MB");
        break;
      case 3:
        ret = _("GB");
        break;
      case 4:
        ret = _("TB");
        break;
    }

    return ret;
  }

  // black magic to get declarative value (mb, gb)
  std::string get_value(uint64_t bytes)
  {
    int cnt (log10f(bytes)/log10f(1024));
    uint64_t factor = ::pow(1024ul, (uint64_t)cnt);
    float rest = cnt > 0.0f ? (((float)bytes / (float)factor)/10.0f) : 0.0f;
    std::stringstream str;

    uint64_t ret = factor == 0ul ? ret : bytes / factor;
    str << (ret+rest) << " " << mnemonic(cnt);
    return str.str();
  }

}

void
DownloadMeterImpl :: set_status ()
{
  // check if limit reached
  if (_downloaded_bytes > _limit && _limit > 0)
  {
    fire_dl_limit_reached ();
    if (_prefs.get_flag("warn-dl-limit-reached", true))
      _view->set_color("red");
  }
  else
  {
    _view->reset_color ();
  }

  _progress->set_status_va(_("DL %s"), get_value(_downloaded_bytes).c_str());

  if (_limit > 0)
    _progress->set_step (100ul * _downloaded_bytes/_limit);

}

void
DownloadMeterImpl :: dl_meter_add (uint64_t bytes)
{
    _downloaded_bytes += bytes;
    fire_xfer_bytes(bytes);
    set_status ();
}

void
DownloadMeterImpl :: dl_meter_reset ()
{
  std::cerr<<"reset\n";
  _downloaded_bytes = 0;
  set_status ();
  fire_reset_xfer_bytes ();
}

void
DownloadMeterImpl :: dl_meter_init (uint64_t bytes)
{
   _downloaded_bytes = bytes;
}

void
DownloadMeterImpl :: dl_meter_update ()
{
  std::string limit_type (_prefs.get_string("dl-limit-type", ""));

  int type_idx (0);
  if (limit_type == "mb")
      type_idx = 2; // 1024*1024
  if (limit_type == "gb")
      type_idx = 3; // 1024*1024*1024

  int limit (_prefs.get_int("dl-limit", 1024));
  _limit = limit * ::pow(1024, type_idx);

  set_status ();
}

void
DownloadMeterImpl :: fire_reset_xfer_bytes ()
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_reset_xfer_bytes ();
}

void
DownloadMeterImpl :: fire_dl_limit_reached ()
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_dl_limit_reached ();
}


void
DownloadMeterImpl :: fire_xfer_bytes (uint64_t bytes)
{
  for (lit it(_listeners.begin()), end(_listeners.end()); it!=end; )
    (*it++)->on_xfer_bytes (bytes);
}

uint64_t
DownloadMeterImpl :: dl_meter_get_limit()
{
  return _limit;
}

void
DownloadMeterImpl :: dl_meter_set_limit (uint64_t l)
{
  _limit = l;
  set_status();
}

