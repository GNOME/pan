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

#ifndef __NNTP_Pool_h__
#define __NNTP_Pool_h__

#include <vector>
#include <string>
#include <pan/general/quark.h>
#include <pan/data/server-info.h>
#include <pan/tasks/socket.h>
#include <pan/tasks/nntp.h>
#include <pan/tasks/socket-impl-main.h>
#include <pan/gui/prefs.h>
#include <pan/data/data.h>

#ifdef HAVE_GNUTLS
  #include <pan/data/cert-store.h>
#endif

namespace pan {
struct WorkerPool;

/**
 * A pool of NNTP connections to a particular server.
 *
 * @ingroup tasks
 */
class NNTP_Pool: public NNTP::Source,
		private NNTP::Listener,
		private Socket::Creator::Listener,
		private CertStore::Listener {
public:

	NNTP_Pool(const Quark & server, ServerInfo & server_info, Prefs& prefs, SocketCreator *,
			CertStore &);
	virtual ~NNTP_Pool();

	void check_in(NNTP*, Health) override;
	NNTP* check_out();
	void abort_tasks();
	void kill_tasks();
	void idle_upkeep();

	void get_counts(int& setme_active, int& setme_idle, int& setme_connecting,
			int& setme_max) const;

public:

	/** Interface class for objects that listen to an NNTP_Pool's events */
	class Listener {
	public:
		virtual ~Listener() {
		}
		;
		virtual void on_pool_has_nntp_available(const Quark& server) = 0;
		virtual void on_pool_error(const Quark& server,
				const StringView& message) = 0;
	};

	void add_listener(Listener * l) {
		_listeners.insert(l);
	}
	void remove_listener(Listener * l) {
		_listeners.erase(l);
	}
	void request_nntp(WorkerPool&);

private:
	//  NNTP::Listener
	virtual void on_nntp_done(NNTP*, Health, const StringView&);

private:
	// Socket::Creator::Listener
	void on_socket_created(const StringView& host, int port, bool ok,	Socket*) override;
	void on_socket_shutdown(const StringView& host, int port, Socket*) override
  {}
#ifdef HAVE_GNUTLS
private:
	// CertStore::Listener
	void on_verify_cert_failed(gnutls_x509_crt_t, std::string, int) override;
	void on_valid_cert_added(gnutls_x509_crt_t, std::string) override;
#endif
private:

	void fire_pool_has_nntp_available() {
		for (listeners_t::iterator it(_listeners.begin()),
				end(_listeners.end()); it != end;)
			(*it++)->on_pool_has_nntp_available(_server);
	}
	void fire_pool_error(const StringView& message) {
		for (listeners_t::iterator it(_listeners.begin()),
				end(_listeners.end()); it != end;)
			(*it++)->on_pool_error(_server, message);
	}

	ServerInfo& _server_info;
	const Quark _server;
	SocketCreator * _socket_creator;
	int _pending_connections;
	CertStore& _certstore;
	Prefs& _prefs;

	struct PoolItem {
		NNTP * nntp;
		bool is_checked_in;
		time_t last_active_time;
	};
	typedef std::vector<PoolItem> pool_items_t;
	pool_items_t _pool_items;
	int _active_count;

private:

	time_t _time_to_allow_new_connections;
	bool new_connections_are_allowed() const;
	void disallow_new_connections_for_n_seconds(int n);
	void allow_new_connections();

	typedef std::set<Listener*> listeners_t;
	listeners_t _listeners;
};
}
;

#endif // __NNTP_Pool_h__
