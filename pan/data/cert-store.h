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

#ifdef HAVE_OPENSSL
  #include <openssl/pem.h>
  #include <openssl/err.h>
  #include <openssl/pkcs12.h>
  #include <openssl/bio.h>
  #include <openssl/rand.h>
  #include <openssl/x509.h>
#endif

#include <pan/data/data.h>

#include <pan/tasks/socket.h>
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
#ifdef HAVE_OPENSSL
    public:
      CertStore (Data& data) ;
      virtual ~CertStore () ;

    private:
      SSL_CTX* _ctx;
      typedef std::set<Quark> certs_t;
      certs_t _certs;
      typedef std::map<Quark,X509*> certs_m;
      typedef std::pair<Quark,X509*> certs_p;
      certs_m _cert_to_server;
      X509_STORE* _store;
      std::string _path;
      std::vector<SSL_SESSION*> _sessions;
      certs_t _blacklist;
      Data& _data;

    public:
      SSL_CTX* get_ctx() { return _ctx; }
      X509_STORE* get_store() const { return _store; }
      int get_all_certs_from_disk(std::set<X509*>& setme);
      X509* get_cert_to_server(const Quark& server) const;
      SSL_SESSION* get_session()
      {
        SSL_SESSION* ret(0);
        if (!_sessions.empty())
        {
          ret = _sessions.back();
          _sessions.pop_back();
        }
        return ret;
      }
      void add_session (SSL_SESSION* s)
      {
        if (!s) return;
        _sessions.push_back(s);
      }

      bool in_blacklist (const Quark& s)
      {
        return _blacklist.count(s) != 0;
      }
      void blacklist (const Quark& s)
      {
        _blacklist.insert(s);
      }
      void whitelist (const Quark& s)
      {
        _blacklist.erase(s);
      }

      void dump_blacklist()
      {
        std::cerr<<"#################\n";
        std::cerr<<_blacklist.size()<<std::endl;
        std::cerr<<"#################\n\n";
      }

      void dump_certs()
      {
        std::cerr<<"#################\n";
        foreach_const(certs_t, _certs, it)
          std::cerr<<*it<<"\n";
        std::cerr<<"#################\n\n";
      }

    private:
      void remove_hard(const Quark&);

    public:

      bool add(X509*, const Quark&) ;
      void remove (const Quark&);
      bool exist (const Quark& q) { /*dump_certs(); std::cerr<<"q "<<q<<"\n\n"; */ return (_certs.count(q) > 0); }

      static std::string build_cert_name(std::string host);

      struct Listener
      {
        virtual ~Listener() {}
        /* functions that other listeners listen on */
        virtual void on_verify_cert_failed (X509* cert UNUSED, std::string server UNUSED, std::string cert_name UNUSED, int nr UNUSED) = 0;
        virtual void on_valid_cert_added (X509* cert UNUSED, std::string server UNUSED) = 0;
      };

      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;

      void add_listener (Listener * l)    { _listeners.insert(l); }
      void remove_listener (Listener * l) { _listeners.erase(l);  }

      /* notify functions for listener list */
      void verify_failed (X509* c, std::string server, std::string cn, int nr)
      {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; ++it)
          (*it)->on_verify_cert_failed (c, server, cn, nr);
      }

      void valid_cert_added (X509* c, std::string server)
      {
        for (listeners_t::iterator it(_listeners.begin()), end(_listeners.end()); it!=end; ++it)
          (*it)->on_valid_cert_added (c, server);
      }

    private:
      void init_me();

    protected:
      friend class SocketCreator;
      void set_ctx(SSL_CTX* c) { _ctx = c; init_me(); }
  };

  struct mydata_t {
   SSL_CTX* ctx;
   int depth;
   int ignore_all;
   CertStore* cs;
   std::string server;
   std::string cert_name;
   CertStore::Listener* l;

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

#endif   // HAVE_OPENSSL
  };

}


#endif

