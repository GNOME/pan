
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

#include <pan/usenet-utils/ssl-utils.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/locking.h>
#include <pan/general/macros.h>
#include <pan/general/worker-pool.h>
#include <pan/general/string-view.h>
#include "socket-impl-main.h"

#ifdef G_OS_WIN32
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0501
  #include <ws2tcpip.h>
  #undef gai_strerror
#endif

using namespace pan;

namespace
{
  const unsigned int TIMEOUT_SECS (10);

  struct ThreadWorker : public WorkerPool::Worker,
                        public WorkerPool::Worker::Listener
  {
    std::string host;
    int port;
    Socket::Creator::Listener * listener;

    bool ok;
    ServerInfo& data;
    Socket * socket;
    std::string err;
    bool use_ssl;
    const Quark server;
#ifdef HAVE_OPENSSL
    std::multimap<std::string, Socket*>& socket_map;
    SSL_CTX * context;
    CertStore& store;
    ThreadWorker (ServerInfo& d, const Quark& s, const StringView& h, int p, Socket::Creator::Listener *l,
                  bool ssl, SSL_CTX* ctx, CertStore& cs, std::multimap<std::string, Socket*>& m):
      data(d), server(s), host(h), port(p), listener(l), ok(false), socket(0), use_ssl(ssl), context(ctx), store(cs), socket_map(m) {}
#else
    ThreadWorker (ServerInfo& d, const Quark& s, const StringView& h, int p, Socket::Creator::Listener *l):
      data(d), server(s), host(h), port(p), listener(l), ok(false), socket(0), use_ssl(false) {}
#endif

    void do_work ()
    {
#ifdef HAVE_OPENSSL
        if (use_ssl)
        {
          socket = new GIOChannelSocketSSL (data, server, context, store);
          socket_map.insert(std::pair<std::string, Socket*>(host, socket));
        }
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

// TODO remove this later if it works with GMutex
#ifdef G_OS_WIN32
  #define MUTEX_TYPE HANDLE
  #define MUTEX_LOCK(x) WaitForSingleObject((x), INFINITE)
  #define MUTEX_UNLOCK(x) ReleaseMutex(x)
  #define THREAD_ID GetCurrentThreadId( )
#else
  #define MUTEX_TYPE Mutex
  #define MUTEX_LOCK(x) x.lock()
  #define MUTEX_UNLOCK(x) x.unlock()
  #define THREAD_ID pthread_self( )
#endif
  #define MUTEX_SETUP(x) (x) = new MUTEX_TYPE[CRYPTO_num_locks()];
  #define MUTEX_CLEANUP(x) delete [] x
namespace
{
  static MUTEX_TYPE* mutex;

  void gio_lock(int mode, int type, const char *file, int line)
  {
    if (mode & CRYPTO_LOCK)
      MUTEX_LOCK(mutex[type]);//.lock();
    else
      MUTEX_UNLOCK(mutex[type]);//.unlock();
  }

  void ssl_thread_setup() {
    MUTEX_SETUP(mutex);
#ifdef G_OS_WIN32
    for (int i = 0; i < CRYPTO_num_locks(); ++i) mutex[i] = CreateMutex(NULL, FALSE, NULL);
#endif
    CRYPTO_set_locking_callback(gio_lock);
  }

  void ssl_thread_cleanup() {
#ifdef G_OS_WIN32
    for (int i = 0; i < CRYPTO_num_locks(); ++i) CloseHandle(mutex[i]);
#endif
    MUTEX_CLEANUP(mutex);
    CRYPTO_set_locking_callback(0);
  }

}
#endif

SocketCreator :: SocketCreator(Data& d, CertStore& cs) : data(d), store(cs)
{

#ifdef HAVE_OPENSSL
  SSL_library_init();
  SSL_load_error_strings();
  OpenSSL_add_all_algorithms();
  ERR_load_crypto_strings();

  /* init static locks for threads */
  ssl_thread_setup();
  ssl_ctx = SSL_CTX_new(SSLv3_client_method());
  cs.set_ctx(ssl_ctx);
  SSL_CTX_set_mode(ssl_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_AUTO_RETRY);
  SSL_CTX_set_session_cache_mode(ssl_ctx, SSL_SESS_CACHE_CLIENT);

  cs.add_listener(this);
#endif
}


SocketCreator :: ~SocketCreator()
{
#ifdef HAVE_OPENSSL
  store.remove_listener(this);

  ssl_thread_cleanup();
  if (ssl_ctx) SSL_CTX_free(ssl_ctx);
#endif
}

void
SocketCreator :: create_socket (ServerInfo& info,
                                const StringView & host,
                                int                port,
                                WorkerPool       & threadpool,
                                Socket::Creator::Listener * listener,
                                bool               use_ssl)
{
    Quark server;
    data.find_server_by_hn(host, server);
    ensure_module_init ();
    if (store.in_blacklist(server)) return;
#ifdef HAVE_OPENSSL
    ThreadWorker * w = new ThreadWorker (info, server, host, port, listener, use_ssl, ssl_ctx, store, socket_map);
#else
    ThreadWorker * w = new ThreadWorker (info, server, host, port, listener);
#endif
    threadpool.push_work (w, w, true);
}

#ifdef HAVE_OPENSSL
void
SocketCreator :: on_verify_cert_failed(X509* cert, std::string server, std::string cert_name, int nr)
{}

void
SocketCreator :: on_valid_cert_added (X509* cert, std::string server)
{}
#endif
