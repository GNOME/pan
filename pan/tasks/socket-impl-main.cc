
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
      if (use_ssl)
        socket = new GIOChannelSocketSSL ();
      else
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

SocketCreator :: SocketCreator() {}
SocketCreator :: ~SocketCreator() {}

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
