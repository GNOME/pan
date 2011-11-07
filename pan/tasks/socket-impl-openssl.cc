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

/** Copyright notice: Some code taken from here :
  * http://dslinux.gits.kiev.ua/trunk/user/irssi/src/src/core/network-openssl.c
  * Copyright (C) 2002 vjt (irssi project) */

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
  #include <glib/gi18n.h>
}

#include <pan/usenet-utils/ssl-utils.h>
#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/worker-pool.h>

#ifdef G_OS_WIN32
  // this #define is necessary for mingw
  #define _WIN32_WINNT 0x0501
  #include <ws2tcpip.h>
  #undef gai_strerror

  #define gai_strerror(i) gai_strerror_does_not_link (i)
//  const char*
//  gai_strerror_does_not_link (int errval)
//  {
//    char buf[32];
//    g_snprintf (buf, sizeof(buf), "Winsock error %d", errval);
//    return buf;
//  }

  const char*
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
#include "socket-impl-openssl.h"
#include "socket-impl-main.h"
#include "cert-store.h"

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

#ifdef HAVE_OPENSSL // without libssl this class is just a stub....

GIOChannelSocketSSL :: GIOChannelSocketSSL (SSL_CTX* ctx, CertStore& cs):
   _channel (0),
   _tag_watch (0),
   _tag_timeout (0),
   _listener (0),
   _out_buf (g_string_new (0)),
   _in_buf (g_string_new (0)),
   _io_performed (false),
   _ctx(ctx),
   _certstore(cs)
{
//   std::cerr<<"GIOChannelSocketSSL ctor " << (void*)this<<std::endl;
   cs.add_listener(this);
}


GIOChannel *
GIOChannelSocketSSL :: create_channel (const StringView& host_in, int port, std::string& setme_err)
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
      return 0;
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
    return 0;
  }

  GIOChannel * channel (0);
#ifndef G_OS_WIN32
  channel = g_io_channel_unix_new (sockfd);
#else
  channel = g_io_channel_win32_new_socket (sockfd);
#endif
  if (g_io_channel_get_encoding (channel) != 0)
    g_io_channel_set_encoding (channel, 0, 0);
  g_io_channel_set_buffered (channel, true);
  g_io_channel_set_line_term (channel, "\n", 1);
  return ssl_get_iochannel(channel);
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
    SSL *ssl;
    SSL_CTX *ctx;
    char* host;
    unsigned int verify;
  } GIOSSLChannel;


  void ssl_free(GIOChannel *handle)
  {
    GIOSSLChannel *chan = (GIOSSLChannel *)handle;
    g_io_channel_unref(chan->giochan);
    SSL_free(chan->ssl);

    g_free(chan);
  }
}

GIOChannelSocketSSL :: ~GIOChannelSocketSSL ()
{

//  std::cerr << LINE_ID << " destroying socket " << this << std::endl;

  _certstore.remove_listener(this);

  remove_source (_tag_watch);
  remove_source (_tag_timeout);

  if (_channel)
  {
    g_io_channel_shutdown (_channel, true, 0);
    ssl_free(_channel);
    g_string_free(_channel->read_buf,true);
    _channel = 0;
  }

  g_string_free (_out_buf, true);
  _out_buf = 0;

  g_string_free (_in_buf, true);
  _in_buf = 0;
}

bool
GIOChannelSocketSSL :: open (const StringView& address, int port, std::string& setme_err)
{
  _host.assign (address.str, address.len);
  _channel = create_channel (address, port, setme_err);
  return _channel != 0;
}

void
GIOChannelSocketSSL :: get_host (std::string& setme) const
{
  setme = _host;
}

void
GIOChannelSocketSSL :: write_command (const StringView& command, Socket::Listener * l)
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

  static GIOStatus ssl_errno(gint e)
  {
    switch(e)
    {
      case EINVAL:
        return G_IO_STATUS_ERROR;
      case EINTR:
      case EAGAIN:
        return G_IO_STATUS_AGAIN;
      default:
        return G_IO_STATUS_ERROR;
    }
    return G_IO_STATUS_ERROR;
  }


  int ssl_handshake(GIOChannel *handle, CertStore::Listener* listener, CertStore* cs, std::string host)
  {

    GIOSSLChannel *chan = (GIOSSLChannel *)handle;
    int ret;
    int err;
    X509 *cert;
    const char *errstr;

    /* init custom data for callback */
    mydata_t mydata;// = new mydata_t();
    mydata.ctx = chan->ctx;
    mydata.cs = cs;
    mydata.ignore_all = 0;
    mydata.l = listener;
    mydata.server = host;
    SSL_set_ex_data(chan->ssl, SSL_get_fd(chan->ssl), &mydata);

    ret = SSL_connect(chan->ssl);
    if (ret <= 0) {
      err = SSL_get_error(chan->ssl, ret);
      switch (err) {
        case SSL_ERROR_WANT_READ:
          return 1;
        case SSL_ERROR_WANT_WRITE:
          return 3;
        case SSL_ERROR_ZERO_RETURN:
          g_warning("SSL handshake failed: %s", "server closed connection");
          return -1;
        case SSL_ERROR_SYSCALL:
          errstr = ERR_reason_error_string(ERR_get_error());
          if (errstr == NULL && ret == -1)
            errstr = strerror(errno);
          g_warning("SSL handshake failed: %s", errstr != NULL ? errstr : "server closed connection unexpectedly");
          return -1;
        default:
          errstr = ERR_reason_error_string(ERR_get_error());
          g_warning("SSL handshake failed: %s", errstr != NULL ? errstr : "unknown SSL error");
          return -1;
      }
    }

    cert = SSL_get_peer_certificate(chan->ssl);
    if (!cert) {
      g_warning("SSL server supplied no certificate");
      return -1;
    }

    ret = !chan->verify || ssl_verify(chan->ssl, chan->ctx, host.c_str(), cert);
    X509_free(cert);

    return ret ? 0 : -1;

  }

  GIOStatus ssl_read(GIOChannel *handle, gchar *buf, gsize len, gsize *ret, GError **gerr)
  {
    return G_IO_STATUS_NORMAL;
  }

  GIOStatus ssl_read_line_string(GIOChannel *handle, GString* g, gsize *ret, GError **gerr)
  {

    GIOSSLChannel *chan = (GIOSSLChannel *)handle;

    gint err;
    size_t tmp_size(4096*128);
    char tmp[tmp_size];
    g_string_set_size(g,0);

    if (handle->read_buf->len == 0)
    {
      err = SSL_read(chan->ssl, tmp, tmp_size*sizeof(char));
      if (ret) *ret = err < 0 ? 0 : err;
      if(err <= 0)
      {
        if(SSL_get_error(chan->ssl, err) == SSL_ERROR_WANT_READ)
          return G_IO_STATUS_AGAIN;
        return ssl_errno(errno);
      }
      else
        g_string_append_len (handle->read_buf,tmp,err);
    }

    //fill in from read_buf
    char * buf = handle->read_buf->str;
    int pos(0);
    bool found(false);
    while (*buf) { if (*buf == '\n') { found = true; break; } ++pos; ++buf; }
    if (found) {
      int _pos(std::min(pos+1,(int)handle->read_buf->len));
      g_string_append_len(g, handle->read_buf->str, _pos);
      g_string_erase (handle->read_buf, 0, _pos);
      return G_IO_STATUS_NORMAL;
    }
    // no linebreak, partial line. retry later...
    return G_IO_STATUS_AGAIN;
  }

  GIOStatus ssl_write(GIOChannel *handle, const gchar *buf, gsize len, gsize *ret, GError **gerr)
  {
    GIOSSLChannel *chan = (GIOSSLChannel *)handle;
    gint err;

    err = SSL_write(chan->ssl, (const char *)buf, len);
    if(err < 0)
    {
      *ret = 0;
      if(SSL_get_error(chan->ssl, err) == SSL_ERROR_WANT_READ)
        return G_IO_STATUS_AGAIN;
      return ssl_errno(errno);
    }
    else
    {
      *ret = err;
      return G_IO_STATUS_NORMAL;
    }
    return G_IO_STATUS_ERROR;
  }

  GIOStatus ssl_seek(GIOChannel *handle, gint64 offset, GSeekType type, GError **gerr)
  {
    GIOSSLChannel *chan = (GIOSSLChannel *)handle;
    GIOError e;
    e = g_io_channel_seek(chan->giochan, offset, type);
    return (e == G_IO_ERROR_NONE) ? G_IO_STATUS_NORMAL : G_IO_STATUS_ERROR;
  }

  GIOStatus ssl_close(GIOChannel *handle, GError **gerr)
  {
    GIOSSLChannel *chan = (GIOSSLChannel *)handle;
    g_io_channel_close(chan->giochan);

    return G_IO_STATUS_NORMAL;
  }

  GSource *ssl_create_watch(GIOChannel *handle, GIOCondition cond)
  {
    GIOSSLChannel *chan = (GIOSSLChannel *)handle;

    return chan->giochan->funcs->io_create_watch(handle, cond);
  }

  GIOStatus ssl_set_flags(GIOChannel *handle, GIOFlags flags, GError **gerr)
  {
      GIOSSLChannel *chan = (GIOSSLChannel *)handle;

      return chan->giochan->funcs->io_set_flags(handle, flags, gerr);
  }

  GIOFlags ssl_get_flags(GIOChannel *handle)
  {
      GIOSSLChannel *chan = (GIOSSLChannel *)handle;

      return chan->giochan->funcs->io_get_flags(handle);
  }

  GIOFuncs ssl_channel_funcs = {
    ssl_read,
    ssl_write,
    ssl_seek,
    ssl_close,
    ssl_create_watch,
    ssl_free,
    ssl_set_flags,
    ssl_get_flags
  };
}

/***
****
***/

GIOChannelSocketSSL :: DoResult
GIOChannelSocketSSL :: do_read ()
{
  g_assert (!_out_buf->len);

  GError * err (0);
  GString * g (_in_buf);

  bool more (true);

  GIOSSLChannel * chan = (GIOSSLChannel*)_channel;

  while (more && !_abort_flag)
  {
    _io_performed = true;
    gsize ret;
    const GIOStatus status (ssl_read_line_string(_channel, g, &ret, &err));

    if (status == G_IO_STATUS_NORMAL)
    {
      g_string_prepend_len (g, _partial_read.c_str(), _partial_read.size());
      _partial_read.clear ();

      debug_v ("read [" << g->str << "]");
      increment_xfer_byte_count (g->len);
      if (g_str_has_suffix (g->str, "\r\n"))
        g_string_truncate (g, g->len-2);
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
      if (err != 0)
        g_clear_error (&err);
      return IO_ERR;
    }
  }

  return IO_DONE;
}


GIOChannelSocketSSL :: DoResult
GIOChannelSocketSSL :: do_write ()
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
    ? ssl_write(_channel, g->str, g->len, &out, &err)
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
GIOChannelSocketSSL :: timeout_func (gpointer sock_gp)
{
  GIOChannelSocketSSL * self (static_cast<GIOChannelSocketSSL*>(sock_gp));

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
GIOChannelSocketSSL :: gio_func (GIOChannel   * channel,
                              GIOCondition   cond,
                              gpointer       sock_gp)
{
  return static_cast<GIOChannelSocketSSL*>(sock_gp)->gio_func (channel, cond);
}

gboolean
GIOChannelSocketSSL :: gio_func (GIOChannel   * channel,
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
    /* I keep reading about crashes due to this check on OSX.
     * _abort_flag is never set so this won't cause a problem.
     * could be a bug in gcc 4.2.1.
     */
    /*if (_abort_flag)        _listener->on_socket_abort (this);
    else*/ if (result == IO_ERR)   _listener->on_socket_error (this);
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
GIOChannelSocketSSL :: set_watch_mode (WatchMode mode)
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


GIOChannel *
GIOChannelSocketSSL :: ssl_get_iochannel(GIOChannel *handle, gboolean verify)
{

	GIOSSLChannel *chan(0);
	GIOChannel *gchan(0);
	int err(0), fd(0);
	SSL *ssl(0);
	SSL_CTX *ctx(_ctx);

	g_return_val_if_fail(handle != 0, 0);
	g_return_val_if_fail(ctx != 0, 0);

	if(!(fd = g_io_channel_unix_get_fd(handle)))
	{
    return 0;
	}

	if(!(ssl = SSL_new(ctx)))
	{
		g_warning("Failed to allocate SSL structure");
		return 0;
	}

	if(!(err = SSL_set_fd(ssl, fd)))
	{
		g_warning("Failed to associate socket to SSL stream");
		SSL_free(ssl);
		return 0;
	}

	chan = g_new0(GIOSSLChannel, 1);
	if (!chan) return 0;

	chan->fd = fd;
	chan->giochan = handle;
	chan->ssl = ssl;
	chan->ctx = ctx;
	chan->verify = verify ? 1 : 0;

	gchan = (GIOChannel *)chan;
	gchan->funcs = &ssl_channel_funcs;
	g_io_channel_init(gchan);
  gchan->read_buf = g_string_sized_new(4096*128);

  if (ssl_handshake(gchan, this, &_certstore, _host) == 0)
  {
    g_io_channel_set_flags (handle, G_IO_FLAG_NONBLOCK, 0);
    return gchan;
  }
  return 0;
}

void
GIOChannelSocketSSL :: on_verify_cert_failed (X509* cert, std::string server, int nr)
{

}

void
GIOChannelSocketSSL :: on_valid_cert_added (X509* cert, std::string server)
{

}
#endif  //HAVE_OPENSSL

