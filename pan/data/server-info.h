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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __Server_Info_h__
#define __Server_Info_h__

#include <string>
#include <pan/general/quark.h>
#include <pan/general/compression.h>
#include <pan/general/string-view.h>

namespace pan
{

  struct ServerRank
  {
    virtual ~ServerRank () {}
    virtual int get_server_rank (const Quark& server) const=0;
  };

  /**
   * Data Interface class for querying/setting news server data.
   * @ingroup data
   */
  class ServerInfo: public ServerRank
  {

    public:

      virtual ~ServerInfo () {}

    public: // mutators

      virtual void set_server_auth (const Quark       & servername,
                                    const StringView  & username,
                                    gchar             *&password,
                                    bool                use_gkr) = 0;

      virtual void set_server_trust (const Quark       & servername,
                                     const int           setme) = 0;

      virtual void set_server_compression_type (const Quark       & servername,
                                                  const int           setme) = 0;

      virtual void set_server_addr (const Quark       & servername,
                                    const StringView  & address,
                                    const int           port) = 0;

      virtual void set_server_limits (const Quark & server,
                                      int           max_connections)= 0;

      virtual void set_server_article_expiration_age  (const Quark  & server,
                                                       int            days) = 0;

      virtual void set_server_rank (const Quark& server,
                                    int          rank) = 0;

      virtual void set_server_ssl_support (const Quark& server,
                                    int          ssl) = 0;

      virtual void set_server_cert (const Quark & server, const StringView & cert) = 0;

      virtual void save_server_info (const Quark& server) = 0;

    public: // accessors

      virtual bool get_server_auth (const Quark   & servername,
                                    std::string   & setme_username,
                                    gchar         *&setme_password,
                                    bool            use_gkr) = 0;

      virtual bool get_server_trust (const Quark  & servername, int&) const = 0;

      virtual bool get_server_compression_type (const Quark  & servername, CompressionType&) const = 0;

      virtual bool get_server_addr (const Quark   & servername,
                                    std::string   & setme_address,
                                    int           & setme_port) const = 0;

	  // only used for debug and loging output
      virtual std::string get_server_address (const Quark& servername) const = 0;

      virtual bool get_server_ssl_support (const Quark & server) const = 0;

      virtual std::string get_server_cert (const Quark & server) const = 0;

      /** If set_server_limits() has never been called, 2 is returned. */
      virtual int get_server_limits (const Quark & server) const = 0;

      virtual int get_server_article_expiration_age  (const Quark  & server) const = 0;

      virtual bool find_server_by_host_name (const std::string& server, Quark& setme) const = 0;
  };
}

#endif
