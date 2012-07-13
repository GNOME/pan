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

#ifndef __Socket_h__
#define __Socket_h__

#include <string>
#include <config.h>

#ifdef HAVE_GNUTLS
  #include <gnutls/gnutls.h>
#endif

#include <sstream>

namespace pan
{
  class StringView;
  class WorkerPool;
  class Data;

  /**
   * Defines primitive interactions with a remote server:
   * Send command, read response, send command, read response.
   *
   * @ingroup tasks
   */
  class Socket
  {
    public:
      Socket ();
      virtual ~Socket () {}

    public:
      /** Interface class for objects that listen to a Socket's events */
      struct Listener {
        virtual ~Listener () {}
        virtual bool on_socket_response (Socket*, const StringView& line) = 0;
        virtual void on_socket_error (Socket*) = 0;
        virtual void on_socket_abort (Socket*) = 0;
      };

      void write(const std::string& str) { *_stream << str; }
      void clear() { (*_stream).clear(); }
      std::stringstream*& get_stream() { return _stream; }

    public:
      virtual bool open (const StringView& address, int port, std::string& setme_err) = 0;
      virtual void write_command (const StringView& chars, Listener *) = 0;

    public:
      void write_command_va (Listener*, const char * fmt, ...);
      double get_speed_KiBps () const;
      void reset_speed_counter ();
      void set_abort_flag (bool b);
      bool is_abort_set () const;
      virtual void get_host (std::string& setme) const = 0;

    protected:
      void increment_xfer_byte_count (unsigned long byte_count);
      mutable unsigned long _bytes_since_last_check;
      mutable time_t _time_of_last_check;
      mutable double _speed_KiBps;
      bool _abort_flag;
      std::stringstream* _stream;

    public:

      /**
       * Interface class for code that creates sockets.
       *
       * This is currently implemented in glib with the GIOSocketCreator,
       * but can also be implemented for unit tests or ports to other
       * libraries.
       *
       * @ingroup tasks
       */
      struct Creator
      {
        struct Listener {
          virtual ~Listener () {}
          virtual void on_socket_created (const StringView& host, int port, bool ok, Socket*) = 0;
          virtual void on_socket_shutdown (const StringView& host, int port, Socket*) = 0;
        };

        virtual ~Creator () { }
        virtual void create_socket (Data&, const StringView& host, int port, WorkerPool&, Listener*, bool) = 0;
      };
  };
}

#endif
