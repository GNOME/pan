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

#ifndef __NNTP_Pool_h__
#define __NNTP_Pool_h__

#include <vector>
#include <string>
#include <pan/general/quark.h>
#include <pan/data/server-info.h>
#include <pan/tasks/socket.h>
#include <pan/tasks/nntp.h>

namespace pan
{
  /**
   * A pool of NNTP connections to a particular server.
   *
   * @ingroup tasks 
   */
  class NNTP_Pool:
    public NNTP::Source,
    private NNTP::Listener,
    private ServerInfo::Listener,
    private Socket::Creator::Listener
  {
    public:

      NNTP_Pool (const Quark       & server,
                 ServerInfo        & server_info,
                 Socket::Creator   *);

      virtual ~NNTP_Pool ();

      virtual void check_in (NNTP*, bool is_ok);
      NNTP* check_out ();
      void abort_tasks ();
      void idle_upkeep ();

      void get_counts (int& setme_active,
                       int& setme_idle,
                       int& setme_connecting,
                          int& setme_max) const;

    public:

      /** Interface class for objects that listen to an NNTP_Pool's events */
      class Listener {
        public:
          virtual ~Listener () { };
          virtual void on_pool_has_nntp_available (const Quark& server) = 0;
          virtual void on_pool_error (const Quark& server, const std::string& message) = 0;
      };

      void add_listener (Listener * l) { _listeners.insert (l); }
      void remove_listener (Listener * l) { _listeners.erase (l); }
      void request_nntp ();

    private: //  NNTP::Listener
      virtual void on_nntp_done (NNTP*, Health);

    private: // ServerInfo::Listener
      virtual void on_server_limits_changed (const Quark& server, int max_connections);

    private: // Socket::Creator::Listener
      virtual void on_socket_created (const StringView& host, int port, bool ok, Socket*);

    private:

      void fire_pool_has_nntp_available () {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
          (*it++)->on_pool_has_nntp_available (_server);
      }
      void fire_pool_error (const std::string& message) {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; )
          (*it++)->on_pool_error (_server, message);
      }

      ServerInfo& _server_info;
      const Quark _server;
      Socket::Creator * _socket_creator;
      bool _connection_pending;
      int _max_connections;

      struct PoolItem {
        NNTP * nntp;
        bool is_checked_in;
        time_t last_active_time;
      };
      typedef std::vector<PoolItem> pool_items_t;
      pool_items_t _pool_items;
      int _active_count;

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;
  };
};

#endif // __NNTP_Pool_h__
