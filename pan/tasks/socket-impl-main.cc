
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


/***
****  GIOChannel::SocketCreator -- create a socket in a worker thread
***/

#include <string>
#include <glib/giochannel.h>
#include <glib/gstring.h>
#include <pan/tasks/socket.h>

#include <config.h>
#include <iostream>
#include <string>
#include <cerrno>
#include <cstring>

#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/worker-pool.h>
#include <pan/general/string-view.h>

#include "socket-impl-main.h"

using namespace pan;

/*  FIXME for win32!!!!!!!
namespace
{
  void ensure_module_inited (void)
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
*/

namespace pan
{
  struct ThreadWorker : public WorkerPool::Worker,
                        public WorkerPool::Worker::Listener
  {
    std::string host;
    int port;
    Socket::Creator::Listener * listener;

    bool ok;
    Socket * socket;
    std::string err;
    bool use_ssl;

    ThreadWorker (const StringView& h, int p, Socket::Creator::Listener *l, bool ssl):
      host(h), port(p), listener(l), ok(false), socket(0), use_ssl(ssl) {}

    void do_work ()
    {
      #ifdef HAVE_OPENSSL
        if (use_ssl)
          socket = new GIOChannelSocketSSL ();
        else
      #endif
          socket = new GIOChannelSocket ();
      ok = socket->open (host, port, err);
    }

    /** called in main thread after do_work() is done */
    void on_worker_done (bool cancelled UNUSED)
    {
      // pass results to main thread...
      if (!err.empty())   Log :: add_err (err.c_str());
      listener->on_socket_created (host, port, ok, socket);
    }
  };
}


#ifdef HAVE_OPENSSL
namespace
{
  static pthread_mutex_t *lock_cs=0;

  void gio_lock(int mode, int type, const char *file, int line)
  {
    if (mode & CRYPTO_LOCK)
      pthread_mutex_lock(&(lock_cs[type]));
    else
      pthread_mutex_unlock(&(lock_cs[type]));
  }

  void ssl_thread_setup() {
    lock_cs = (pthread_mutex_t*)OPENSSL_malloc(CRYPTO_num_locks() * sizeof(pthread_mutex_t));
    for (int i=0; i<CRYPTO_num_locks(); i++)
      if (pthread_mutex_init(&lock_cs[i],0) != 0)
        g_warning("error initialing mutex!");

    CRYPTO_set_locking_callback(gio_lock);
  }

  void ssl_thread_cleanup() {
    for (int i=0; i<CRYPTO_num_locks(); i++)
      pthread_mutex_destroy(&lock_cs[i]);
    CRYPTO_set_locking_callback(0);
    CRYPTO_set_id_callback(0);
    OPENSSL_free(lock_cs);
  }
}
#endif

SocketCreator :: SocketCreator()
{
#ifdef HAVE_OPENSSL
  SSL_library_init();
  SSL_load_error_strings();
  /* init static locks for threads */
  ssl_thread_setup();
#endif

}
SocketCreator :: ~SocketCreator()
{
#ifdef HAVE_OPENSSL
  ssl_thread_cleanup();
#endif
}

void
SocketCreator :: create_socket (const StringView & host,
                                int                port,
                                WorkerPool       & threadpool,
                                Socket::Creator::Listener * listener,
                                bool               use_ssl)
{
//  ensure_module_inited ();

  ThreadWorker * w = new ThreadWorker (host, port, listener, use_ssl);
  threadpool.push_work (w, w, true);
}
