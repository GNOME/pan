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

#ifndef __SocketSSL_h__
#define __SocketSSL_h__

#include <string>
#include <glib/giochannel.h>
#include <glib/gstring.h>
#include <pan/general/quark.h>
#include <pan/tasks/socket.h>
#include <pan/tasks/socket-impl-gio.h>

#ifdef HAVE_OPENSSL
  #include <pan/data-impl/cert-store.h>
  #include <openssl/crypto.h>
  #include <openssl/x509.h>
  #include <openssl/x509v3.h>
  #include <openssl/pem.h>
  #include <openssl/ssl.h>
  #include <openssl/err.h>
#endif


namespace pan
{
  /**
   * glib implementation of Socket
   *
   * @ingroup tasks
   */
#ifdef HAVE_OPENSSL
  class GIOChannelSocketSSL:
    public GIOChannelSocket,
    private CertStore::Listener
  {
    public:
      virtual ~GIOChannelSocketSSL ();
      GIOChannelSocketSSL (SSL_CTX* ctx, CertStore& cs);

      virtual bool open (const StringView& address, int port, std::string& setme_err);
      virtual void write_command (const StringView& chars, Socket::Listener *);
      virtual void get_host (std::string& setme) const;

    private:
      GIOChannel * _channel;
      unsigned int _tag_watch;
      unsigned int _tag_timeout;
      Socket::Listener * _listener;
      GString * _out_buf;
      GString * _in_buf;
      std::string _partial_read;
      std::string _host;
      bool _io_performed;
      SSL_CTX * _ctx;
      CertStore& _certstore;
      SSL_SESSION* _session;
      bool _rehandshake;

    public:
      void set_rehandshake (bool setme) { _rehandshake = setme; }

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
      virtual void on_verify_cert_failed (X509*, std::string, int) ;
      virtual void on_valid_cert_added (X509*, std::string );

      GIOChannel * create_channel (const StringView& host_in, int port, std::string& setme_err);
      void gio_lock(int mode, int type, const char *file, int line);

    private:
      GIOChannel* ssl_get_iochannel(GIOChannel *handle, gboolean verify=true);

#else
  class GIOChannelSocketSSL
  {
    public:
      virtual ~GIOChannelSocketSSL ();
      GIOChannelSocketSSL ();
#endif  // HAVE_OPENSSL
  };
}

#endif
