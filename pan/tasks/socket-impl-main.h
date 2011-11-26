/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2011 Heinrich Müller <sphemuel@stud.informatik.uni-erlangen.de>
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

#include <pan/data/cert-store.h>
#include "socket-impl-gio.h"

namespace
{
  typedef int (*t_getaddrinfo)(const char *,const char *, const struct addrinfo*, struct addrinfo **);
  typedef void (*t_freeaddrinfo)(struct addrinfo*);
}

namespace
{

  static t_getaddrinfo p_getaddrinfo (0);
  static t_freeaddrinfo p_freeaddrinfo (0);

  static void ensure_module_init (void)
  {
    static bool inited (false);

    if (!inited)
    {
      p_freeaddrinfo=NULL;
      p_getaddrinfo=NULL;

#ifdef G_OS_WIN32
      WSADATA wsaData;
      WSAStartup(MAKEWORD(2,2), &wsaData);

      char sysdir[MAX_PATH], path[MAX_PATH+8];

      if(GetSystemDirectory(sysdir,MAX_PATH)!=0)
      {
        HMODULE lib=NULL;
        FARPROC pfunc=NULL;
        const char *libs[]={"ws2_32","wship6",NULL};

        for(const char **p=libs;*p!=NULL;++p)
        {
          g_snprintf(path,MAX_PATH+8,"%s\\%s",sysdir,*p);
          lib=LoadLibrary(path);
          if(!lib)
            continue;
          pfunc=GetProcAddress(lib,"getaddrinfo");
          if(!pfunc)
          {
            FreeLibrary(lib);
            lib=NULL;
            continue;
          }
          p_getaddrinfo=reinterpret_cast<t_getaddrinfo>(pfunc);
          pfunc=GetProcAddress(lib,"freeaddrinfo");
          if(!pfunc)
          {
            FreeLibrary(lib);
            lib=NULL;
            p_getaddrinfo=NULL;
            continue;
          }
          p_freeaddrinfo=reinterpret_cast<t_freeaddrinfo>(pfunc);
          break;
        }
      }
#else
      p_freeaddrinfo=::freeaddrinfo;
      p_getaddrinfo=::getaddrinfo;
#endif
      inited = true;
    }
  }
}

namespace pan
{

  class SocketCreator:
    private CertStore::Listener,
    private Socket::Creator::Listener
  {
    public:
      SocketCreator (Data&, CertStore&);
      virtual ~SocketCreator ();


    private:
      //socket::creator::Listener
      virtual void on_socket_created (const StringView& host, int port, bool ok, Socket*) {}
      virtual void on_socket_shutdown (const StringView& host, int port, Socket*)
      {
#ifdef HAVE_OPENSSL

#endif
      }

#ifdef HAVE_OPENSSL
      SSL_CTX* ssl_ctx;
      std::multimap<std::string, Socket*> socket_map;

      // CertStore::Listener
      virtual void on_verify_cert_failed(X509*, std::string, std::string, int);
      virtual void on_valid_cert_added (X509*, std::string );
#endif
      CertStore & store;
      Data& data;

    public:
      virtual void create_socket (const StringView & host,
                                  int                port,
                                  WorkerPool       & threadpool,
                                  Socket::Creator::Listener * listener,
                                  bool               use_ssl);

      struct Listener
      {
        virtual ~Listener() {}
        /* functions that other listeners listen on */
        virtual void on_handshake_done (X509* cert UNUSED, std::string server UNUSED, std::string cert_name UNUSED, int nr UNUSED) = 0;
      };

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

      void add_listener (Listener * l)    { _listeners.insert(l); }
      void remove_listener (Listener * l) { _listeners.erase(l);  }

      /* notify functions for listener list */
      void handshake_done (X509* c, std::string server, std::string cn, int nr)
      {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; ++it)
          (*it)->on_handshake_done (c, server, cn, nr);
      }

  };

}

#endif
