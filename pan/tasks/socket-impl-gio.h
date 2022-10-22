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

#ifndef __SocketGIO_h__
#define __SocketGIO_h__

#include <string>
#include <glib.h>
#include <pan/tasks/socket.h>

namespace pan
{
  /**
   * glib implementation of Socket
   *
   * @ingroup tasks
   */
  class GIOChannelSocket: public Socket
  {
    public:
      GIOChannelSocket ();
      virtual ~GIOChannelSocket ();
      bool open (const StringView& address, int port, std::string& setme_err) override;
      void get_host (std::string& setme) const override;

    private:
      GIOChannel * _channel;
      unsigned int _tag_watch;
      unsigned int _tag_timeout;
      Listener * _listener;
      GString * _out_buf;
      GString * _in_buf;
      std::string _partial_read;
      std::string _host;
      bool _io_performed;

    private:
      friend class GIOChannelSocketSSL;
      enum WatchMode { READ_NOW, WRITE_NOW, IGNORE_NOW };
      void set_watch_mode (WatchMode mode);
      void write_command (const StringView& chars, Listener *) override;
      static gboolean gio_func (GIOChannel*, GIOCondition, gpointer);
      gboolean gio_func (GIOChannel*, GIOCondition);
      static gboolean timeout_func (gpointer);
      enum DoResult { IO_ERR, IO_READ, IO_WRITE, IO_DONE };
      DoResult do_read ();
      DoResult do_write ();
  };
}

#endif
