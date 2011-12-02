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
  #include <glib/gi18n.h>
  #include <gio/gio.h>
}

#include <pan/general/file-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/worker-pool.h>

#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/gnksa.h>
#include "socket-impl-gio.h"

using namespace pan;

namespace
{
  const unsigned int TIMEOUT_SECS (30);
}

namespace
{

  GIOChannel *
  create_channel (const StringView& host_in, int port, std::string& setme_err)
  {

    GIOChannel * channel (0);

#ifndef G_OS_WIN32
    signal (SIGPIPE, SIG_IGN);
#endif

    const std::string host (host_in.str, host_in.len);
    char portbuf[32], hpbuf[255];
    g_snprintf (portbuf, sizeof(portbuf), "%d", port);
    g_snprintf (hpbuf, sizeof(hpbuf), "%s:%s", host_in.str, portbuf);

    GSocketClient * client = g_socket_client_new ();
    g_return_val_if_fail(client, 0);

    GSocketConnection * conn = g_socket_client_connect_to_host
      (client, hpbuf, 119, 0, NULL);
    g_return_val_if_fail(conn, 0);

    GSocket* g(g_socket_connection_get_socket(conn));
    g_return_val_if_fail(g, 0);

    int sockfd = g_socket_get_fd(g);

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
    g_io_channel_shutdown (_channel, TRUE, NULL);
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
    /* I keep reading about crashes due to this check on OSX.
     * _abort_flag is never set so this won't cause a problem.
     * could be a bug in gcc 4.2.1.
     */
    /*if (_abort_flag)        _listener->on_socket_abort (this);
    else*/ if (result == IO_ERR) _listener->on_socket_error (this);
    else if (result == IO_READ)  set_watch_mode (READ_NOW);
    else if (result == IO_WRITE) set_watch_mode (WRITE_NOW);
  }

  return false; // set_watch_now(IGNORE) cleared the tag that called this func
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
    void on_worker_done (bool cancelled UNUSED)
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
  ThreadWorker * w = new ThreadWorker (host, port, listener);
  threadpool.push_work (w, w, true);
}
