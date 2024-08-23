/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2011 Heinrich Müller <henmull@src.gnome.org>
 * SSL functions : Copyright (C) 2002 vjt (irssi project)
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

#ifndef __SocketSSL_h__
#define __SocketSSL_h__

#include <string>
//#include <glib/giochannel.h>
//#include <glib/gstring.h>
#include <glib.h>

#include <pan/general/debug.h>
#include <pan/general/quark.h>
#include <pan/tasks/socket.h>
#include <pan/tasks/socket-impl-gio.h>
#include <pan/tasks/socket-impl-main.h>

#ifdef HAVE_GNUTLS
  #include <pan/data/cert-store.h>
  #include <gnutls/gnutls.h>
  #include <gnutls/x509.h>
#endif

namespace pan
{
  /**
   * glib implementation of Socket
   *
   * @ingroup tasks
   */
#ifdef HAVE_GNUTLS
  class GIOChannelSocketGnuTLS:
    public Socket,
    private CertStore::Listener
  {
    public:
      virtual ~GIOChannelSocketGnuTLS ();
      GIOChannelSocketGnuTLS (ServerInfo&, const Quark&, CertStore& cs);

      bool open (const StringView& address, int port, std::string& setme_err) override;
      void write_command (const StringView& chars, Socket::Listener *) override;
      void get_host (std::string& setme) const override ;

    private:
      ServerInfo& _data;
      GIOChannel * _channel;
      unsigned int _tag_watch;
      unsigned int _tag_timeout;
      Socket::Listener * _listener;
      GString * _out_buf;
      GString * _in_buf;
      std::string _partial_read;
      std::string _host;
      bool _io_performed;
      CertStore& _certstore;
      bool _rehandshake;
      Quark _server;
      bool _done;

    private:
      enum WatchMode { READ_NOW, WRITE_NOW, IGNORE_NOW };
      void set_watch_mode (WatchMode mode);
      static gboolean gio_func (GIOChannel*, GIOCondition, gpointer);
      gboolean gio_func (GIOChannel*, GIOCondition);
      static gboolean timeout_func (gpointer);
      enum DoResult { IO_ERR, IO_READ, IO_WRITE, IO_DONE };
      DoResult do_read ();
      DoResult do_write ();

      // CertStore::Listener
      void on_verify_cert_failed (gnutls_x509_crt_t, std::string, int) override;
      void on_valid_cert_added (gnutls_x509_crt_t, std::string ) override;

      GIOChannel * create_channel (const StringView& host_in, int port, std::string& setme_err);
      void gio_lock(int mode, int type, const char *file, int line);

    private:
      GIOChannel* gnutls_get_iochannel(GIOChannel* channel, const char* host, gboolean verify=true);
      GIOStatus  _gnutls_handshake (GIOChannel *channel);
      gboolean verify_certificate (gnutls_session_t session, GError **err);
      static gboolean handshake_cb(gpointer ptr);
      GIOStatus gnutls_read_line(GString* g, gsize *ret, GError **gerr);
      GIOStatus gnutls_write_line(GIOChannel *handle, const gchar *buf, gsize len, gsize *ret, GError **gerr);

#else
  class GIOChannelSocketGnuTLS
  {
    public:
      virtual ~GIOChannelSocketGnuTLS ();
      GIOChannelSocketGnuTLS () { pan_debug("SocketSSL stub ctor"); }
#endif  // HAVE_GNUTLS

  };
}

#endif
