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

#ifndef _DOWNLOAD_METER_IMPL_H_
#define _DOWNLOAD_METER_IMPL_H_

#include <string>
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/gui/prefs.h>
#include <pan/gui/progress-view.h>
#include <pan/general/progress.h>
#include <pan/data/data.h>

namespace pan
{
  class DownloadMeterImpl :
            public virtual DownloadMeter
  {
    public:

      DownloadMeterImpl (Prefs&, Data&);
      virtual ~DownloadMeterImpl() ;

      virtual void dl_meter_add(const uint64_t bytes) ;
      virtual void dl_meter_init (uint64_t bytes) ;
      virtual void dl_meter_reset () ;
      virtual void dl_meter_update () ;

    private:

      typedef std::set<DownloadMeter::Listener*> listeners_t;
      typedef listeners_t::const_iterator lit;
      listeners_t _listeners;

      ProgressView* _view;
      Progress* _progress;
      uint64_t _limit;
      uint64_t _downloaded_bytes;
      GtkWidget* _widget;
      GtkWidget* _button;
      Prefs& _prefs;

    public:

      virtual void add_listener (DownloadMeter::Listener * l)    { _listeners.insert(l); }
      virtual void remove_listener (DownloadMeter::Listener * l) { _listeners.erase(l);  }

      virtual ProgressView* get_view() { return _view; }
      virtual GtkWidget* get_widget () { return _widget; }
      virtual GtkWidget* get_button () { return _button; }

    public:

      virtual uint64_t dl_meter_get_limit();
      virtual void dl_meter_set_limit (uint64_t l);

      virtual uint64_t dl_meter_get_bytes() const { return _downloaded_bytes; }

    private:

      virtual void fire_xfer_bytes (uint64_t bytes);
      virtual void fire_reset_xfer_bytes ();
      virtual void fire_dl_limit_reached ();

      void set_status ();


  };
}

#endif
