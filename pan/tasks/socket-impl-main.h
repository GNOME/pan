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


#ifndef __SocketMAIN_h__
#define __SocketMAIN_h__

#ifdef G_OS_WIN32
  #include <ws2tcpip.h>
#else
  #include <signal.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
#endif

#include <pan/general/string-view.h>
#include <pan/general/worker-pool.h>
#include "socket.h"
#ifdef HAVE_OPENSSL
  #include <openssl/crypto.h>
  #include <openssl/ssl.h>
  #include "socket-impl-openssl.h"
#endif
#include "socket-impl-gio.h"

namespace pan
{

  typedef int (*t_getaddrinfo)(const char *,const char *, const struct addrinfo*, struct addrinfo **);
  static t_getaddrinfo p_getaddrinfo (0);

  typedef void (*t_freeaddrinfo)(struct addrinfo*);
  static t_freeaddrinfo p_freeaddrinfo (0);

}

namespace pan
{

  class SocketCreator
  {
    public:
      SocketCreator ();
      virtual ~SocketCreator ();

      virtual void create_socket (const StringView & host,
                                  int                port,
                                  WorkerPool       & threadpool,
                                  Socket::Creator::Listener * listener,
                                  bool               use_ssl);
  };

}

#endif
