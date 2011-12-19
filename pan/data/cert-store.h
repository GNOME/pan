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

#ifndef __CertStore_h__
#define __CertStore_h__

#ifdef HAVE_GNUTLS
  #include <gnutls/gnutls.h>
  #include <gnutls/x509.h>
#endif

#include <pan/data/data.h>
#include <pan/tasks/socket.h>
#include <pan/general/debug.h>
#include <pan/general/quark.h>
#include <pan/general/macros.h>
#include <pan/general/worker-pool.h>
#include <pan/general/string-view.h>
#include <map>
#include <iostream>


namespace pan
{
  class Data;

  class CertStore
  {
#ifdef HAVE_GNUTLS
    public:
      CertStore (Data& data) ;
      virtual ~CertStore () ;

    private:
      typedef std::set<Quark> certs_t;
      certs_t _certs;
      certs_t _blacklist;
      typedef std::map<Quark,gnutls_x509_crt_t> certs_m;
      typedef std::pair<Quark,gnutls_x509_crt_t> certs_p;
      std::string _path;
      certs_m _cert_to_server;
      Data& _data;

      gnutls_certificate_credentials_t _creds;

    public:

      int get_all_certs_from_disk();

      bool in_blacklist (const Quark& s)
      {
        return _blacklist.count(s);
      }

      void blacklist (const Quark& s)
      {
        _blacklist.insert(s);
      }

      void whitelist (const Quark& s)
      {
        _blacklist.erase(s);
      }

      gnutls_x509_crt_t get_cert_to_server (const Quark& s)
      {
        if (_cert_to_server.count(s) > 0)
          return _cert_to_server[s];
        std::cerr<<"server "<<s<<" cert to server "<<_cert_to_server.count(s)<<"\n";
        foreach (certs_m, _cert_to_server, it)
          std::cerr<<it->first<<" "<<it->second<<"\n";
        return 0;
      }

    private:
      void remove_hard(const Quark&);

    public:

      bool add (gnutls_x509_crt_t, const Quark&) ;
      void remove (const Quark&);
      bool exist (const Quark& q) { return (_certs.count(q) > 0); }

      static std::string build_cert_name(std::string& host);

      gnutls_certificate_credentials_t get_creds() { return _creds; }

      struct Listener
      {
        virtual ~Listener() {}
        /* functions that other listeners listen on */
        virtual void on_verify_cert_failed (gnutls_x509_crt_t cert UNUSED, std::string server UNUSED, int nr UNUSED) = 0;
        virtual void on_valid_cert_added   (gnutls_x509_crt_t cert UNUSED, std::string server UNUSED) = 0;
      };

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

      void add_listener (Listener * l)    { _listeners.insert(l); }
      void remove_listener (Listener * l) { _listeners.erase(l);  }

      /* notify functions for listener list */
      void verify_failed (gnutls_x509_crt_t c, std::string server, int nr)
      {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; ++it)
          (*it)->on_verify_cert_failed (c, server, nr);
      }

      void valid_cert_added (gnutls_x509_crt_t c, std::string server)
      {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; ++it)
          (*it)->on_valid_cert_added (c, server);
      }

    public:
      void init();
  };

  struct mydata_t {
   gnutls_session_t session;
   Quark host;
   Quark hostname_full;
   CertStore* cs;
   int always_trust;
  };
#else

  public:
    CertStore (Data&) {};
    virtual ~CertStore () {};

    void add_listener (void * l) {}
    void remove_listener (void * l) {}
    bool in_blacklist (const Quark& s) { return false; }

    struct Listener
    {
      virtual ~Listener() {}
    };
  };
#endif   // HAVE_GNUTLS
}


#endif

