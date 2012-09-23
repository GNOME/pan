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

#ifndef DL_PREFS_UI_H
#define DL_PREFS_UI_H

#include "gtk-compat.h"
#include <pan/gui/prefs.h>
#include <pan/data/data.h>

namespace pan
{

  class DLMeterDialog
  {

    public:

    public:
      DLMeterDialog (Prefs&, DownloadMeter&, GtkWindow*) ;
      ~DLMeterDialog () { }

      Prefs& prefs () { return _prefs; }

      DownloadMeter& meter () { return _meter; }

      GtkWidget* root() { return _root; }

      uint64_t get_limit () { return _meter.dl_meter_get_limit(); }

      GtkWidget* get_enable () { return _enable; }

      GtkWidget* get_spin () { return _spin; }

    private:
      Prefs& _prefs;
      GtkWidget* _root;
      DownloadMeter& _meter;
      GtkWidget* _enable;
      GtkWidget* _spin;

  };
}

#endif
