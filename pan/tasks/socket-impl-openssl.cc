/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2011 Heinrich Müller <henmull@src.gnome.org>
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

#define DEBUG_SOCKET_IO

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
  #include <errno.h>
  #include <fcntl.h>
  #include <sys/time.h>
  #include <sys/types.h>
}

#include <glib/gi18n.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/worker-pool.h>

#ifdef G_OS_WIN32
  // this #define is necessary for mingw
  #undef _WIN32_WINNT
  #define _WIN32_WINNT 0x0501
  #include <ws2tcpip.h>
  #include <winsock2.h>
  #undef gai_strerror

  #define gai_strerror(i) gai_strerror_does_not_link (i)
//  const char*
//  gai_strerror_does_not_link (int errval)
//  {
//    char buf[32];
//    g_snprintf (buf, sizeof(buf), "Winsock error %d", errval);
//    return buf;
//  }


  static std::string
  get_last_error (int err, char const *hpbuf)
  {
    std::string msg;
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
    return msg + " (" + hpbuf + ")";
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
#include "socket-impl-openssl.h"
#include "socket-impl-main.h"
#include <pan/data/cert-store.h>

using namespace pan;

#ifndef G_OS_WIN32
extern t_getaddrinfo p_getaddrinfo;
extern t_freeaddrinfo p_freeaddrinfo;
#endif

/****
*****
*****
*****
****/

namespace
{
  static gboolean gnutls_inited = FALSE;
}

#ifdef HAVE_GNUTLS // without gnutls this class is just a stub....

GIOChannelSocketGnuTLS :: GIOChannelSocketGnuTLS (ServerInfo& data, const Quark& server, CertStore& cs):
   _data(data),
   _channel (nullptr),
   _tag_watch (0),
   _tag_timeout (0),
   _listener (nullptr),
   _out_buf (g_string_new (nullptr)),
   _in_buf (g_string_new (nullptr)),
   _io_performed (false),
   _certstore(cs),
   _server(server),
   _done(false)
{

  pan_debug ("GIOChannelSocketGnuTLS ctor " << (void*)this);
  cs.add_listener(this);
}


GIOChannel *
GIOChannelSocketGnuTLS :: create_channel (const StringView& host_in, int port, std::string& setme_err)
{
  int err;
  int sockfd;

#ifndef G_OS_WIN32
  signal (SIGPIPE, SIG_IGN);
#endif

  // get an addrinfo for the host
  const std::string host (host_in.str, host_in.len);
  char portbuf[32], hpbuf[255];
  g_snprintf (portbuf, sizeof(portbuf), "%d", port);
  g_snprintf (hpbuf,sizeof(hpbuf),"%s:%s",host_in.str,portbuf);

#ifdef G_OS_WIN32 // windows might not have getaddrinfo...
  if (!p_getaddrinfo)
  {
    struct hostent * ans = isalpha (host[0])
      ? gethostbyname (host.c_str())
      : gethostbyaddr (host.c_str(), host.size(), AF_INET);

    err = WSAGetLastError();
    if (err || !ans) {
      setme_err = get_last_error (err, hpbuf);
      return nullptr;
    }

    // try opening the socket
    sockfd = socket (AF_INET, SOCK_STREAM, 0 /*IPPROTO_TCP*/);
    if (sockfd < 0)
      return nullptr;

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
      err = ::connect (sockfd,(struct sockaddr*)&server, sizeof(server));
      pan_debug ("connect "<<err<<" "<<i);
    }

    if (err) {
      closesocket (sockfd);
      setme_err = get_last_error (err, hpbuf);
      return nullptr;
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
    err = ::getaddrinfo (host.c_str(), portbuf, &hints, &ans);
    if (err != 0) {
      char buf[512];
      snprintf (buf, sizeof(buf), _("Error connecting to \"%s\""), hpbuf);
      setme_err = buf;
      if (errno) {
        setme_err += " (";
        setme_err += file :: pan_strerror (errno);
        setme_err += ")";
      }
      return nullptr;
    }

    // try to open a socket on any ipv4 or ipv6 addresses we found
    errno = 0;
    sockfd = -1;
    for (struct addrinfo * walk(ans); walk && sockfd<=0; walk=walk->ai_next)
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
    ::freeaddrinfo (ans);
  }

  // create the giochannel...
  if (sockfd <= 0) {
    char buf[512];
    snprintf (buf, sizeof(buf), _("Error connecting to \"%s\""), hpbuf);
    setme_err = buf;
    if (errno) {
      setme_err += " (";
      setme_err += file :: pan_strerror (errno);
      setme_err += ")";
    }
    return nullptr;
  }

  GIOChannel * channel (nullptr);
#ifndef G_OS_WIN32
  channel = g_io_channel_unix_new (sockfd);
  g_io_channel_set_flags (channel, G_IO_FLAG_NONBLOCK, nullptr);
#else
  channel = g_io_channel_win32_new_socket (sockfd);
#endif
  if (g_io_channel_get_encoding (channel) != nullptr)
    g_io_channel_set_encoding (channel, nullptr, nullptr);
  g_io_channel_set_buffered (channel,true);
  g_io_channel_set_line_term (channel, "\n", 1);
  GIOChannel* ret (gnutls_get_iochannel(channel, host_in.str));
  pan_debug ("########### SocketSSL "<<ret);
  return ret;
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

namespace
{

  typedef struct
  {
    GIOChannel pad;
    gint fd;
    GIOChannel *giochan;
    char* host;
    bool verify;
    gnutls_session_t session;
    gnutls_certificate_credentials_t cred;
    bool established;
  } GIOGnuTLSChannel;


  void _gnutls_free(GIOChannel *handle)
  {
    GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)handle;
    g_io_channel_unref(chan->giochan);
    // free callback struct
    delete (mydata_t*)gnutls_session_get_ptr (chan->session);
    gnutls_deinit (chan->session);
    g_free(chan->host);
    g_free(chan);
  }
}

GIOChannelSocketGnuTLS :: ~GIOChannelSocketGnuTLS ()
{

  _certstore.remove_listener(this);

  pan_debug(" destroying SSL socket "<<this);

  remove_source (_tag_watch);
  remove_source (_tag_timeout);

  if (_channel)
  {
    g_io_channel_shutdown (_channel, true, nullptr);
    g_string_free(_channel->read_buf,true);
    _gnutls_free(_channel);
    _channel = nullptr;
  }

  g_string_free (_out_buf, true);
  _out_buf = nullptr;

  g_string_free (_in_buf, true);
  _in_buf = nullptr;
}

bool
GIOChannelSocketGnuTLS :: open  (const StringView& address, int port, std::string& setme_err)
{
  _host.assign (address.str, address.len);
  _channel = create_channel (address, port, setme_err);
  if (_channel)
  {
#ifdef G_OS_WIN32
      _id = g_io_channel_win32_get_fd(_channel);
#else
      _id = g_io_channel_unix_get_fd(_channel);
#endif // G_OS_WIN32
  }
  return _channel != nullptr;
}

void
GIOChannelSocketGnuTLS :: get_host (std::string& setme) const
{
  setme = _host;
}

void
GIOChannelSocketGnuTLS :: write_command (const StringView& command, Socket::Listener * l)
{
  _partial_read.clear ();
  _listener = l;

  g_string_truncate (_out_buf, 0);
  if (!command.empty())
    g_string_append_len (_out_buf, command.str, command.len);

  set_watch_mode (WRITE_NOW);
}

/***
**** SSL Functions
***/

namespace
{

  static void set_blocking(gnutls_session_t& session, bool val)
  {
    int fd(-1);
    gnutls_transport_ptr_t tmp = gnutls_transport_get_ptr (session);
    fd = GPOINTER_TO_INT (tmp);

    if(fd)
    {
#ifndef G_OS_WIN32
      int flags = fcntl(fd, F_GETFL);
      if (!val)
        flags |= O_NONBLOCK;
      else
        flags &= ~O_NONBLOCK;
      fcntl(fd, F_SETFL, flags);
    }
#else
      u_long block(val ? 0 : 1);
      ioctlsocket(fd, FIONBIO, &block);
    }
#endif
  }

  GIOStatus _gnutls_read(GIOChannel *handle, gchar *buf, gsize len, gsize *ret, GError **gerr)
  {
    return G_IO_STATUS_NORMAL;
  }

  GIOStatus _gnutls_write(GIOChannel *handle, const gchar *buf, gsize len, gsize *ret, GError **gerr)
  {
    return G_IO_STATUS_NORMAL;
  }

  GIOStatus gnutls_seek(GIOChannel *handle, gint64 offset, GSeekType type, GError **gerr)
  {
    GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)handle;
    g_io_channel_seek_position(chan->giochan, offset, type, gerr);
    return !gerr ? G_IO_STATUS_NORMAL : G_IO_STATUS_ERROR;
  }

  GIOStatus gnutls_close(GIOChannel *handle, GError **gerr)
  {
    pan_debug("gnutls close "<<handle);

    GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *) handle;

    if (chan->established) {
      int ret;

      do {
        ret = gnutls_bye (chan->session, GNUTLS_SHUT_WR);
      } while (ret == GNUTLS_E_INTERRUPTED);
    }

    return chan->giochan->funcs->io_close (handle, gerr);
  }

  GSource *gnutls_create_watch(GIOChannel *handle, GIOCondition cond)
  {
    GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)handle;

    return chan->giochan->funcs->io_create_watch(handle, cond);
  }

  GIOStatus gnutls_set_flags(GIOChannel *handle, GIOFlags flags, GError **gerr)
  {
      GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)handle;

      return chan->giochan->funcs->io_set_flags(handle, flags, gerr);
  }

  GIOFlags gnutls_get_flags(GIOChannel *handle)
  {
      GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)handle;

      return chan->giochan->funcs->io_get_flags(handle);
  }

  GIOFuncs gnutls_channel_funcs = {
    _gnutls_read,
    _gnutls_write,
    gnutls_seek,
    gnutls_close,
    gnutls_create_watch,
    _gnutls_free,
    gnutls_set_flags,
    gnutls_get_flags
  };


}

/***
****
***/

GIOStatus
GIOChannelSocketGnuTLS :: gnutls_write_line(GIOChannel *handle, const gchar *buf, gsize len, gsize *ret, GError **gerr)
{

  *ret = 0;

  GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)handle;
  gint err;
  GIOStatus result;

  if (!chan->established) {
    result = _gnutls_handshake (handle);

    if (result == G_IO_STATUS_AGAIN ||
        result == G_IO_STATUS_ERROR)
          return result;
      chan->established = TRUE;
  }

  err = gnutls_record_send (chan->session, (const char *)buf, len);
  if(err < 0)
  {
    if ((err == GNUTLS_E_INTERRUPTED) ||
        (err == GNUTLS_E_AGAIN))
          return G_IO_STATUS_AGAIN;
    g_set_error (gerr, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Received corrupted data");
  }
  else
  {
    *ret = err;
    return G_IO_STATUS_NORMAL;
  }
  return G_IO_STATUS_ERROR;
}

GIOStatus
GIOChannelSocketGnuTLS :: gnutls_read_line(GString* g, gsize *ret, GError **gerr)
{

  *ret = 0;

  GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)_channel;

  if (!chan->established) {
    GIOStatus result = _gnutls_handshake (_channel);

    if (result == G_IO_STATUS_AGAIN ||
        result == G_IO_STATUS_ERROR)
      return result;

    chan->established = true;
  }

  gint err;
  const size_t tmp_size(4096*128);
  char tmp[tmp_size];
  g_string_set_size(g,0);

  if (_channel->read_buf->len == 0)
  {
    err = gnutls_record_recv (chan->session, tmp, tmp_size);
    *ret = err < 0 ? 0 : err;
    if(err < 0)
    {
      if ((err == GNUTLS_E_INTERRUPTED) ||
          (err == GNUTLS_E_AGAIN))
            return G_IO_STATUS_AGAIN;
      g_set_error (gerr, G_IO_CHANNEL_ERROR, G_IO_CHANNEL_ERROR_FAILED, "Received corrupted data");
      return G_IO_STATUS_ERROR;
    }
    else
      g_string_append_len (_channel->read_buf,tmp,err);
  }

  //fill in from read_buf
  char * buf = _channel->read_buf->str;
  int pos(0);
  bool found(false);
  while (*buf) { if (*buf == '\n') { found = true; break; } ++pos; ++buf; }
  if (found) {
    int _pos(std::min(pos+1,(int)_channel->read_buf->len));
    g_string_append_len(g, _channel->read_buf->str, _pos);
    g_string_erase (_channel->read_buf, 0, _pos);
    return G_IO_STATUS_NORMAL;
  }
  //no linebreak, partial line. retry later...
  return G_IO_STATUS_AGAIN;
}


GIOStatus
GIOChannelSocketGnuTLS :: _gnutls_handshake (GIOChannel *channel)
{

  GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)channel;

  g_return_val_if_fail (channel, G_IO_STATUS_ERROR);
  g_return_val_if_fail (chan, G_IO_STATUS_ERROR);
  g_return_val_if_fail (chan->session, G_IO_STATUS_ERROR);

  /* init custom data for callback */
  mydata_t* mydata = new mydata_t();

  mydata->cs = &_certstore;
  Quark setme;
  _data.find_server_by_host_name(_host, setme);
  mydata->host = setme;
  mydata->hostname_full = _host;
  int res(0); // always trust server cert
  _data.get_server_trust (setme, res);
  mydata->always_trust = res;
  gnutls_session_set_ptr (chan->session, (void *) mydata);

  int status = gnutls_handshake (chan->session);

  bool r = !chan->verify || status == 0;
  if (r) chan->established = true;

  return r ? G_IO_STATUS_NORMAL : G_IO_STATUS_ERROR;

}

GIOChannelSocketGnuTLS :: DoResult
GIOChannelSocketGnuTLS :: do_read ()
{
  g_assert (!_out_buf->len);

  GError * err (nullptr);
  GString * g (_in_buf);

  bool more (true);

//  GIOGnuTLSChannel * chan = (GIOGnuTLSChannel*)_channel;

  while (more && !_abort_flag)
  {
    _io_performed = true;
    gsize ret;
    const GIOStatus status (gnutls_read_line(g, &ret, &err));

    if (status == G_IO_STATUS_NORMAL)
    {
      g_string_prepend_len (g, _partial_read.c_str(), _partial_read.size());
      _partial_read.clear ();

      pan_debug_v ("read [" << g->str << "]");
      increment_xfer_byte_count (g->len);
      //if (g_str_has_suffix (g->str, "\r\n"))
      //  g_string_truncate (g, g->len-2);
      more = _listener->on_socket_response (this, StringView (g->str, g->len));
    }
    else if (status == G_IO_STATUS_AGAIN)
    {
      // see if we've got a partial line buffered up
      if (_channel->read_buf->len != 0)  {
        _partial_read.append (_channel->read_buf->str, _channel->read_buf->len);
        g_string_set_size (_channel->read_buf, 0);
      }
      return IO_READ;
    }
    else
    {
      const char * msg (err ? err->message : _("Unknown Error"));
      Log::add_err_va (_("Error reading from %s: %s"), _host.c_str(), msg);
      if (err != nullptr)
        g_clear_error (&err);
      return IO_ERR;
    }
  }

  return IO_DONE;
}


GIOChannelSocketGnuTLS :: DoResult
GIOChannelSocketGnuTLS :: do_write ()
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
  GError * err = nullptr;
  gsize out = 0;
  GIOStatus status = g->len
    ? gnutls_write_line(_channel, g->str, g->len, &out, &err)
    : G_IO_STATUS_NORMAL;
  pan_debug ("socket " << this << " channel " << _channel
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
GIOChannelSocketGnuTLS :: timeout_func (gpointer sock_gp)
{
  GIOChannelSocketGnuTLS * self (static_cast<GIOChannelSocketGnuTLS*>(sock_gp));

  if (!self->_io_performed)
  {
    pan_debug ("error: channel " << self->_channel << " not responding.");
    gio_func (self->_channel, G_IO_ERR, sock_gp);
    return false;
  }

  // wait another TIMEOUT_SECS and check again.
  self->_io_performed = false;
  return true;
}

gboolean
GIOChannelSocketGnuTLS :: gio_func (GIOChannel   * channel,
                              GIOCondition   cond,
                              gpointer       sock_gp)
{
  return static_cast<GIOChannelSocketGnuTLS*>(sock_gp)->gio_func (channel, cond);
}

gboolean
GIOChannelSocketGnuTLS :: gio_func (GIOChannel   * channel,
                                 GIOCondition   cond)
{
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
    if (result == IO_ERR)   _listener->on_socket_error (this);
    else if (result == IO_READ)  set_watch_mode (READ_NOW);
    else if (result == IO_WRITE) set_watch_mode (WRITE_NOW);
  }

  pan_debug ("gio_func: sock " << this << ", channel " << channel << ", cond " << (cond==G_IO_IN ? "IN" : "OUT"));

  return false; // set_watch_mode(IGNORE) cleared the tag that called this func
}

namespace
{
  const unsigned int TIMEOUT_SECS (30);
}

void
GIOChannelSocketGnuTLS :: set_watch_mode (WatchMode mode)
{
  GIOGnuTLSChannel *chan = (GIOGnuTLSChannel *)_channel;
  pan_debug ("socket " << this << " calling set_watch_mode " << mode << "; _channel is " << chan->giochan);
  remove_source (_tag_watch);
  remove_source (_tag_timeout);

  guint cond;
  switch (mode)
  {
    case IGNORE_NOW:
      // don't add any watches
      pan_debug("channel " << chan->giochan << " setting mode **IGNORE**");
      break;

    case READ_NOW:
      pan_debug("channel " << chan->giochan << " setting mode read");
      cond = (int)G_IO_IN | (int)G_IO_ERR | (int)G_IO_HUP | (int)G_IO_NVAL;
      _tag_watch = g_io_add_watch (chan->giochan, (GIOCondition)cond, gio_func, this);
      _tag_timeout = g_timeout_add (TIMEOUT_SECS*1000, timeout_func, this);
      _io_performed = false;
      break;

    case WRITE_NOW:
      pan_debug("channel " << chan->giochan << " setting mode write");
      cond = (int)G_IO_OUT | (int)G_IO_ERR | (int)G_IO_HUP | (int)G_IO_NVAL;
      _tag_watch = g_io_add_watch (chan->giochan, (GIOCondition)cond, gio_func, this);
      _tag_timeout = g_timeout_add (TIMEOUT_SECS*1000, timeout_func, this);
      _io_performed = false;
      break;
  }

  pan_debug ("set_watch_mode " << (mode==READ_NOW?"READ":mode==WRITE_NOW?"WRITE":"IGNORE") << ": _tag_watch is now " << _tag_watch);
}

GIOChannel *
GIOChannelSocketGnuTLS :: gnutls_get_iochannel(GIOChannel* channel, const char* host, gboolean verify)
{

  g_return_val_if_fail(channel, nullptr);

	GIOGnuTLSChannel *chan(nullptr);
	GIOChannel *gchan(nullptr);
	int fd(0);

	chan = g_new0(GIOGnuTLSChannel, 1);
	g_return_val_if_fail(chan, nullptr);

  gnutls_session_t session(NULL);

	if(!(fd = g_io_channel_unix_get_fd(channel))) return nullptr;

  if (gnutls_init (&session, GNUTLS_CLIENT) != 0) return nullptr;
  if (gnutls_set_default_priority (session) != 0) return nullptr;

  gnutls_priority_set_direct (
  session,
  // "NONE:+VERS-SSL3.0:+CIPHER-ALL:+COMP-ALL:+RSA:+DHE-RSA:+DHE-DSS:+MAC-ALL"
  // "NONE:+VERS-TLS1.0:+CIPHER-ALL:+COMP-ALL:+RSA:+DHE-RSA:+DHE-DSS:+MAC-ALL", NULL); // prefer tls 1.0 for now....
  "NONE:+VERS-TLS-ALL:+CIPHER-ALL:+COMP-ALL:+KX-ALL:SIGN-ALL:+CURVE-ALL:+CTYPE-ALL:+MAC-ALL", NULL); // enable all TLS versions

  gnutls_certificate_credentials_t creds = _certstore.get_creds();
  gnutls_credentials_set (session, GNUTLS_CRD_CERTIFICATE, creds);
  gnutls_transport_set_ptr (session,  GINT_TO_POINTER (fd));

	chan->host = g_strdup(host);
	chan->session = session;
	chan->fd = fd;
	chan->giochan = channel;
	chan->verify = verify;

	gchan = (GIOChannel *)chan;
	gchan->funcs = &gnutls_channel_funcs;
	g_io_channel_init(gchan);
  gchan->read_buf = g_string_sized_new(4096*128);

  set_blocking(session, true);

  if (_gnutls_handshake(gchan) == G_IO_STATUS_NORMAL)
  {
    set_blocking(session, false);
    return gchan;
  }

  set_blocking(session, false);

  return nullptr;
}

void
GIOChannelSocketGnuTLS :: on_verify_cert_failed (gnutls_x509_crt_t cert, std::string server, int nr)
{
   debug_SSL("on_verify_cert_failed "<<server<<" "<<nr);
  _certstore.blacklist(server);
}

void
GIOChannelSocketGnuTLS :: on_valid_cert_added (gnutls_x509_crt_t cert, std::string server)
{}
#endif  //HAVE_GNUTLS

