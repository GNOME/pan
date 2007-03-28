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

/* #define DEBUG_SOCKET_IO */

/******
*******
******/

#include <config.h>
#include <iostream>
#include <string>
#include <cerrno>
#include <cstring>

extern "C" {
  #include <unistd.h>
  #include <glib.h>
  #include <glib/gi18n.h>
}

#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/worker-pool.h>

#ifdef G_OS_WIN32
  // this #define is necessary for mingw
  #define _WIN32_WINNT 0x0501
  #include <ws2tcpip.h>
  #undef gai_strerror
  #define gai_strerror(i) gai_strerror_does_not_link (i)
  static const char*
  gai_strerror_does_not_link (int errval)
  {
    static char buf[32];
    g_snprintf (buf, sizeof(buf), "Winsock error %d", errval);
    return buf;
  }

  static const char*
  get_last_error (int err)
  {
    const char * msg = 0;
    switch(err) {
      case WSANOTINITIALISED: msg = "No successful WSAStartup call yet."; break;
      case WSAENETDOWN: msg = "The network subsystem has failed."; break;
      case WSAEADDRINUSE: msg = "Fully qualified address already bound"; break;
      case WSAEADDRNOTAVAIL: msg = "The specified address is not a valid address for this computer."; break;
      case WSAEFAULT: msg = "Error in socket address"; break;
      case WSAEINPROGRESS: msg = "A call is already in progress"; break;
      case WSAEINVAL: msg = "The socket is already bound to an address."; break;
      case WSAENOBUFS: msg = "Not enough buffers available, too many connections."; break;
      case WSAENOTSOCK: msg = "The descriptor is not a socket."; break;
      case 11001: msg = "Host not found"; break;
      default: msg = "Connect failed";
    }
    return msg;
  }

#else
  #include <signal.h>
  #include <sys/types.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <netdb.h>
  #include <arpa/inet.h>
  #define closesocket(fd) close(fd)
#endif

#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/gnksa.h>
#include "socket-impl-gio.h"

using namespace pan;

namespace
{
  typedef int (*t_getaddrinfo)(const char *,const char *, const struct addrinfo*, struct addrinfo **);
  t_getaddrinfo p_getaddrinfo (0);

  typedef void (*t_freeaddrinfo)(struct addrinfo*);
  t_freeaddrinfo p_freeaddrinfo (0);

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

  GIOChannel *
  create_channel (const StringView& host_in, int port, std::string& setme_err)
  {
    int err;
    int sockfd;

#ifndef G_OS_WIN32
    signal (SIGPIPE, SIG_IGN);
#endif

    // get an addrinfo for the host
    const std::string host (host_in.str, host_in.len);
    char portbuf[32];
    g_snprintf (portbuf, sizeof(portbuf), "%d", port);

#ifdef G_OS_WIN32 // windows might not have getaddrinfo...
    if (!p_getaddrinfo)
    {
      struct hostent * ans = isalpha (host[0])
        ? gethostbyname (host.c_str())
        : gethostbyaddr (host.c_str(), host.size(), AF_INET);

      err = WSAGetLastError();
      if (err || !ans) {
        setme_err = get_last_error (err);
        return 0;
      }

      // try opening the socket
      sockfd = socket (AF_INET, SOCK_STREAM, 0 /*IPPROTO_TCP*/);
      if (sockfd < 0)
        return 0;

      // Try connecting
      int i = 0;
      err = -1;
      struct sockaddr_in server;
      memset (&server, 0, sizeof(struct sockaddr_in));
      while (err && ans->h_addr_list[i])
      {
        char *addr = ans->h_addr_list[i];
        memcpy (&server.sin_addr, addr, ans->h_length);
        server.sin_family = AF_INET;
        server.sin_port = htons(port);
        ++i;
        err = connect (sockfd,(struct sockaddr*)&server, sizeof(server));
      }

      if (err) {
        closesocket (sockfd);
        setme_err = get_last_error (err);
        return 0;
      }
    }
    else
#endif // #ifdef G_OS_WIN32 ...
    {
      errno = 0;
      struct addrinfo hints;
      memset (&hints, 0, sizeof(struct addrinfo));
      hints.ai_flags = 0;
      hints.ai_family = 0;
      hints.ai_socktype = SOCK_STREAM;
      struct addrinfo * ans;
      err = p_getaddrinfo (host.c_str(), portbuf, &hints, &ans);
      if (err != 0) {
        char buf[512];
        snprintf (buf, sizeof(buf), _("Error connecting to \"%s\""), host.c_str());
        setme_err = buf;
        if (errno) {
          setme_err += " (";
          setme_err += file :: pan_strerror (errno);
          setme_err += ")";
        }
        return 0;
      }

      // try to open a socket on any ipv4 or ipv6 addresses we found
      errno = 0;
      sockfd = -1;
      for (struct addrinfo * walk(ans); walk && sockfd<0; walk=walk->ai_next)
      {
        // only use ipv4 or ipv6 addresses
        if ((walk->ai_family!=PF_INET) && (walk->ai_family!=PF_INET6))
          continue;

        // try to create a socket...
        sockfd = ::socket (walk->ai_family, walk->ai_socktype, walk->ai_protocol);
        if (sockfd < 0)
          continue;

        // and make a connection
        if (::connect (sockfd, walk->ai_addr, walk->ai_addrlen) < 0) {
          closesocket (sockfd);
          sockfd = -1;
        }
      }

      // cleanup
      p_freeaddrinfo (ans);
    }

    // create the giochannel...
    if (sockfd < 0) {
      char buf[512];
      snprintf (buf, sizeof(buf), _("Error connecting to \"%s\""), host.c_str());
      setme_err = buf;
      if (errno) {
        setme_err += " (";
        setme_err += file :: pan_strerror (errno);
        setme_err += ")";
      }
      return 0;
    }

    GIOChannel * channel (0);
#ifndef G_OS_WIN32
    channel = g_io_channel_unix_new (sockfd);
    g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, NULL);
#else
    channel = g_io_channel_win32_new_socket (sockfd);
#endif
    if (g_io_channel_get_encoding (channel) != NULL)
      g_io_channel_set_encoding (channel, NULL, NULL);
    g_io_channel_set_buffered (channel, true);
    g_io_channel_set_line_term (channel, "\n", 1);
    return channel;
  }
}

/****
*****
*****
*****
****/

GIOChannelSocket :: GIOChannelSocket ():
   _channel (0),
   _tag_watch (0),
   _tag_timeout (0),
   _listener (0),
   _out_buf (g_string_new (NULL)),
   _in_buf (g_string_new (NULL)),
   _io_performed (false)
{
   debug ("GIOChannelSocket ctor " << (void*)this);
}

namespace
{
  void remove_source (guint& tag) {
    if (tag) {
      g_source_remove (tag);
      tag = 0;
    }
  }
}

GIOChannelSocket :: ~GIOChannelSocket ()
{
//std::cerr << LINE_ID << " destroying socket " << this << std::endl;

  remove_source (_tag_watch);
  remove_source (_tag_timeout);

  if (_channel)
  {
    g_io_channel_close (_channel);
    g_io_channel_unref (_channel);
    _channel = 0;
  }

  g_string_free (_out_buf, TRUE);
  _out_buf = 0;

  g_string_free (_in_buf, TRUE);
  _in_buf = 0;
}

bool
GIOChannelSocket :: open (const StringView& address, int port, std::string& setme_err)
{
  _host.assign (address.str, address.len);
  _channel = create_channel (address, port, setme_err);
  return _channel != 0;
}

void
GIOChannelSocket :: get_host (std::string& setme) const
{
  setme = _host;
}

void
GIOChannelSocket :: write_command (const StringView& command, Listener * l)
{
  _partial_read.clear ();
  _listener = l;

  g_string_truncate (_out_buf, 0);
  if (!command.empty())
    g_string_append_len (_out_buf, command.str, command.len);

  set_watch_mode (WRITE_NOW);
}

/***
****
***/

GIOChannelSocket :: DoResult
GIOChannelSocket :: do_read ()
{
  g_assert (!_out_buf->len);

  GError * err (0);
  GString * g (_in_buf);

  bool more (true);
  while (more && !_abort_flag)
  {
    _io_performed = true;
    const GIOStatus status (g_io_channel_read_line_string (_channel, g, NULL, &err));

    if (status == G_IO_STATUS_NORMAL)
    {
      g_string_prepend_len (g, _partial_read.c_str(), _partial_read.size());
      _partial_read.clear ();

      debug_v ("read [" << g->str << "]"); // verbose debug, if --debug --debug was on the command-line
      increment_xfer_byte_count (g->len);
      if (g_str_has_suffix (g->str, "\r\n"))
        g_string_truncate (g, g->len-2);
      more = _listener->on_socket_response (this, StringView (g->str, g->len));
    }
    else if (status == G_IO_STATUS_AGAIN)
    {
      // see if we've got a partial line buffered up
      if (_channel->read_buf) {
        _partial_read.append (_channel->read_buf->str, _channel->read_buf->len);
        g_string_set_size (_channel->read_buf, 0);
      }
      return IO_READ;
    }
    else
    {
      const char * msg (err ? err->message : _("Unknown Error"));
      Log::add_err_va (_("Error reading from %s: %s"), _host.c_str(), msg);
      if (err != NULL)
        g_clear_error (&err);
      return IO_ERR;
    }
  }

  return IO_DONE;
}


GIOChannelSocket :: DoResult
GIOChannelSocket :: do_write ()
{
  g_assert (_partial_read.empty());

  GString * g = _out_buf;

#if 0
  // #ifdef DEBUG_SOCKET_IO
  // -2 to trim out trailing \r\n
  std::cerr << LINE_ID << " channel " << _channel
            << " writing ["<<StringView(g->str,g->len>=2?g->len-2:g->len)<< "]\n";
#endif

  _io_performed = true;
  GError * err = 0;
  gsize out = 0;
  GIOStatus status = g->len
    ? g_io_channel_write_chars (_channel, g->str, g->len, &out, &err)
    : G_IO_STATUS_NORMAL;
  debug ("socket " << this << " channel " << _channel
                   << " maybe wrote [" << g->str << "]; status was " << status);

  if (status == G_IO_STATUS_NORMAL)
    status = g_io_channel_flush (_channel, &err);

  if (err) {
    Log::add_err (err->message);
    g_clear_error (&err);
    return IO_ERR;
  }

  if (out > 0) {
    increment_xfer_byte_count (out);
    g_string_erase (g, 0, out);
  }

  const bool finished = (!g->len) && (status==G_IO_STATUS_NORMAL);
  if (!finished) return IO_WRITE; // not done writing.
  if (_listener) return IO_READ; // listener wants to read the server's response
  return IO_DONE; // done writing and not listening to response.
}

gboolean
GIOChannelSocket :: timeout_func (gpointer sock_gp)
{
  GIOChannelSocket * self (static_cast<GIOChannelSocket*>(sock_gp));

  if (!self->_io_performed)
  {
    debug ("error: channel " << self->_channel << " not responding.");
    gio_func (self->_channel, G_IO_ERR, sock_gp);
    return false;
  }

  // wait another TIMEOUT_SECS and check again.
  self->_io_performed = false;
  return true;
}

gboolean
GIOChannelSocket :: gio_func (GIOChannel   * channel,
                              GIOCondition   cond,
                              gpointer       sock_gp)
{
  return static_cast<GIOChannelSocket*>(sock_gp)->gio_func (channel, cond);
}

gboolean
GIOChannelSocket :: gio_func (GIOChannel   * channel,
                              GIOCondition   cond)
{
  debug ("gio_func: sock " << this << ", channel " << channel << ", cond " << cond);

  set_watch_mode (IGNORE_NOW);

  if (_abort_flag)
  {
    _listener->on_socket_abort (this);
  }
  else if (!(cond & (G_IO_IN | G_IO_OUT)))
  {
    _listener->on_socket_error (this);
  }
  else // G_IO_IN or G_IO_OUT
  {
    const DoResult result = (cond & G_IO_IN) ? do_read () : do_write ();
         if (_abort_flag)        _listener->on_socket_abort (this);
    else if (result == IO_ERR)   _listener->on_socket_error (this);
    else if (result == IO_READ)  set_watch_mode (READ_NOW);
    else if (result == IO_WRITE) set_watch_mode (WRITE_NOW);
  }

  return false; // set_watch_now(IGNORE) cleared the tag that called this func
}

namespace
{
  const unsigned int TIMEOUT_SECS (30);
}

void
GIOChannelSocket :: set_watch_mode (WatchMode mode)
{
  debug ("socket " << this << " calling set_watch_mode " << mode << "; _channel is " << _channel);
  remove_source (_tag_watch);
  remove_source (_tag_timeout);

  guint cond;
  switch (mode)
  {
    case IGNORE_NOW:
      // don't add any watches
      debug("channel " << _channel << " setting mode **IGNORE**");
      break;

    case READ_NOW:
      debug("channel " << _channel << " setting mode read");
      cond = (int)G_IO_IN | (int)G_IO_ERR | (int)G_IO_HUP | (int)G_IO_NVAL;
      _tag_watch = g_io_add_watch (_channel, (GIOCondition)cond, gio_func, this);
      _tag_timeout = g_timeout_add (TIMEOUT_SECS*1000, timeout_func, this);
      _io_performed = false;
      break;

    case WRITE_NOW:
      debug("channel " << _channel << " setting mode write");
      cond = (int)G_IO_OUT | (int)G_IO_ERR | (int)G_IO_HUP | (int)G_IO_NVAL;
      _tag_watch = g_io_add_watch (_channel, (GIOCondition)cond, gio_func, this);
      _tag_timeout = g_timeout_add (TIMEOUT_SECS*1000, timeout_func, this);
      _io_performed = false;
      break;
  }

  debug ("set_watch_mode " << mode << ": _tag_watch is now " << _tag_watch);
}

/***
****  GIOChannel::SocketCreator -- create a socket in a worker thread
***/

namespace
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

    ThreadWorker (const StringView& h, int p, Socket::Creator::Listener *l):
      host(h), port(p), listener(l), ok(false), socket(0) {}

    void do_work ()
    {
      socket = new GIOChannelSocket ();
      ok = socket->open (host, port, err);
    }

    /** called in main thread after do_work() is done */
    void on_worker_done (bool cancelled)
    {
      // pass results to main thread...
      if (!err.empty())   Log :: add_err (err.c_str());
      listener->on_socket_created (host, port, ok, socket);
    }
  };
}

void
GIOChannelSocket :: Creator :: create_socket (const StringView & host,
                                              int                port,
                                              WorkerPool       & threadpool,
                                              Listener         * listener)
{
  ensure_module_inited ();

  ThreadWorker * w = new ThreadWorker (host, port, listener);
  threadpool.push_work (w, w, true);
}
