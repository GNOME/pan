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

#include <config.h>
#include <ctime>
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include "nntp-pool.h"

using namespace pan;

namespace
{
  const int MAX_IDLE_SECS (30);

  const int HUP_IDLE_SECS (90);

  const int TOO_MANY_CONNECTIONS_LOCKOUT_SECS (120);
}

NNTP_Pool :: NNTP_Pool (const Quark        & server,
                        ServerInfo         & server_info,
                        Socket::Creator    * creator):
  _server_info (server_info),
  _server (server),
  _socket_creator (creator),
  _pending_connections (0),
  _active_count (0),
  _time_to_allow_new_connections (0)
{
}

NNTP_Pool :: ~NNTP_Pool ()
{
  foreach (pool_items_t, _pool_items, it) {
    delete it->nntp->_socket;
    delete it->nntp;
  }
}

/***
****
***/

bool
NNTP_Pool :: new_connections_are_allowed () const
{
  return _time_to_allow_new_connections <= time(0);
}

void
NNTP_Pool :: disallow_new_connections_for_n_seconds (int n)
{
  _time_to_allow_new_connections = time(0) + n;
}

void
NNTP_Pool :: allow_new_connections ()
{
  _time_to_allow_new_connections = 0;
}

/***
****
***/

void
NNTP_Pool :: abort_tasks ()
{
  foreach (pool_items_t, _pool_items, it)
    if (!it->is_checked_in)
      it->nntp->_socket->set_abort_flag (true);
}

NNTP*
NNTP_Pool :: check_out ()
{
  NNTP * nntp (0);

  foreach (pool_items_t, _pool_items, it) {
    if (it->is_checked_in) {
      nntp = it->nntp;
      it->is_checked_in = false;
      ++_active_count;
      debug ("nntp " << nntp << " is now checked out");
      break;
    }
  }

  return nntp;
}

void
NNTP_Pool :: check_in (NNTP * nntp, Health health)
{
  debug ("nntp " << nntp << " is being checked in, health is " << health);

  // find this nntp in _pool_items
  pool_items_t::iterator it;
  for (it=_pool_items.begin(); it!=_pool_items.end(); ++it)
    if (it->nntp == nntp)
      break;

  // process the nntp if we have a match
  if (it != _pool_items.end())
  {
    const bool bad_connection = (health == NETWORK_FAILED);
    int active, idle, pending, max;
    get_counts (active, idle, pending, max);
    const bool too_many = (pending + active) > max;
    const bool discard = bad_connection || too_many;

    --_active_count;

    if (discard)
    {
      delete it->nntp->_socket;
      delete it->nntp;
      _pool_items.erase (it);
      if (bad_connection)
        allow_new_connections (); // to make up for this one
    }
    else
    {
      it->is_checked_in = true;
      it->last_active_time = time (NULL);
      fire_pool_has_nntp_available ();
    }
  }
}

/***
****
***/

void
NNTP_Pool :: on_socket_created (const StringView& host, int port, bool ok, Socket* socket)
{
  if (!ok)
  {
    delete socket;
    --_pending_connections;
  }
  else
  {
    // okay, we at least we established a connection.
    // now try to handshake and pass the buck to on_nntp_done().
    std::string user, pass;
    _server_info.get_server_auth (_server, user, pass);
    NNTP * nntp = new NNTP (_server, user, pass, socket);
    nntp->handshake (this); // 
  }
}

void
NNTP_Pool :: on_nntp_done (NNTP* nntp, Health health, const StringView& response)
{
   debug ("NNTP_Pool: on_nntp_done()");

   if (health == COMMAND_FAILED) // news server isn't accepting our connection!
   {
     std::string s (response.str, response.len);
     foreach (std::string, s, it) *it = tolower (*it);

     // too many connections.
     // there doesn't seem to be a reliable way to test for this:
     // response can be 502, 400, or 451... and the error messages
     // vary from server to server
     if (   (s.find ("502") != s.npos)
         || (s.find ("400") != s.npos)
         || (s.find ("451") != s.npos)
         || (s.find ("480") != s.npos) // http://bugzilla.gnome.org/show_bug.cgi?id=409085
         || (s.find ("too many") != s.npos)
         || (s.find ("limit reached") != s.npos)
         || (s.find ("maximum number of connections") != s.npos))
     {
       disallow_new_connections_for_n_seconds (TOO_MANY_CONNECTIONS_LOCKOUT_SECS);
     }
     else
     {
       const std::string addr (_server_info.get_server_address (_server));
       std::string s;
       char buf[4096];
       snprintf (buf, sizeof(buf), _("Unable to connect to \"%s\""), addr.c_str());
       s = buf;
       if (!response.empty()) {
         s += ":\n";
         s.append (response.str, response.len);
       }
       fire_pool_error (s.c_str());
     }
   }

   if (health != OK)
   {
      delete nntp->_socket;
      delete nntp;
      nntp = 0;
   }

   --_pending_connections;

   // if success...
   if (nntp != 0)
   {
      debug ("success with handshake to " << _server << ", nntp " << nntp);

      PoolItem i;
      i.nntp = nntp;
      i.is_checked_in = true;
      i.last_active_time = time (0);
      _pool_items.push_back (i);

      fire_pool_has_nntp_available ();
   }
}

void
NNTP_Pool :: get_counts (int& setme_active,
                         int& setme_idle,
                         int& setme_pending,
                         int& setme_max) const
{
  setme_active = _active_count;
  setme_idle = _pool_items.size() - _active_count;
  setme_max = _server_info.get_server_limits (_server);
  setme_pending  = _pending_connections;
}


void
NNTP_Pool :: request_nntp ()
{
  int active, idle, pending, max;
  get_counts (active, idle, pending, max);

#if 0
  std::cerr << LINE_ID << "server " << _server << ", "
            << "active: " << active << ' '
            << "idle: " << idle << ' '
            << "pending: " << pending << ' '
            << "max: " << max << ' ' << std::endl;
#endif

  if (!idle && ((pending+active)<max) && new_connections_are_allowed())
  {
    debug ("trying to create a socket");

    std::string address;
    int port;
    if (_server_info.get_server_addr (_server, address, port))
    {
      ++_pending_connections;
      _socket_creator->create_socket (address, port, this);
    }
  }
}

/**
***
**/

namespace
{
  class NoopListener: public NNTP::Listener
  {
    private:
      NNTP::Source * source;
      const bool hang_up;

    public:
      NoopListener (NNTP::Source * s, bool b): source(s), hang_up(b) {}
      virtual ~NoopListener() {}
      virtual void on_nntp_done  (NNTP * nntp, Health health, const StringView& response) {
        source->check_in (nntp, hang_up ? NETWORK_FAILED : health);
        delete this;
      }
  };
}

void
NNTP_Pool :: idle_upkeep ()
{
  for (;;)
  {
    PoolItem * item (0);

    const time_t now (time (0));
    foreach (pool_items_t, _pool_items, it) {
      if (it->is_checked_in && ((now - it->last_active_time) > MAX_IDLE_SECS)) {
        item = &*it;
        break;
      }
    }

    // if no old, checked-in items, then we're done
    if (!item)
      break;

    // send a keepalive message to the old, checked-in item we found
    // the noop can trigger changes in _pool_items, so that must be
    // the last thing we do with the 'item' pointer.
    const time_t idle_time_secs = now - item->last_active_time;
    item->is_checked_in = false;
    ++_active_count;
    if (idle_time_secs >= HUP_IDLE_SECS)
      item->nntp->goodbye (new NoopListener (this, true));
    else
      item->nntp->noop (new NoopListener (this, false));
    item = 0;
  }
}
