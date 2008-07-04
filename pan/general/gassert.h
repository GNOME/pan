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

#ifndef __Pan_Assert_h__
#define __Pan_Assert_h__

extern "C"
{
  #include <glibconfig.h> // get the version
  #include <glib/gutils.h> // get GLIB_CHECK_VERSION

  // pick up g_assert()
  #if GLIB_CHECK_VERSION(2,16,0)
    #include <glib/gtestutils.h>
  #else
    #include <glib/gmessages.h>
  #endif
}

#endif
