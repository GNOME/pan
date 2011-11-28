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

#include <string>
#include <glib/giochannel.h>
#include <glib/gstring.h>
#include <pan/tasks/socket.h>
#include <config.h>
#include <map>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>

extern "C" {
  #include <glib/gi18n.h>
}

#include <pan/general/debug.h>
#include <pan/general/e-util.h>
#include <pan/general/macros.h>
#include <pan/usenet-utils/ssl-utils.h>
#include <pan/general/file-util.h>
#include <pan/general/messages.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>

#include "cert-store.h"

using namespace pan;

#ifdef HAVE_OPENSSL
namespace pan
{

  int
  verify_callback(int ok, X509_STORE_CTX *store)
  {

    SSL * ssl = (SSL*)X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());
    mydata_t* mydata = (mydata_t*)SSL_get_ex_data(ssl, SSL_get_fd(ssl));

    if (!ok)
    {
      if (mydata->ignore_all==1) { return 1; }

      X509 *cert = X509_STORE_CTX_get_current_cert(store);
      CRYPTO_add (&(cert->references), 1, CRYPTO_LOCK_X509); // refcount +1
      int err = X509_STORE_CTX_get_error(store);

      /* accept user-override on self-signed certificates */
      if (err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN ||
          err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
          err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
        mydata->cs->verify_failed(cert, mydata->server, mydata->cert_name, err);
      else
        g_warning("[[DEBUG:]] unknown error condition, please report me: %s", ssl_err_to_string(err).c_str());
    }

    return ok;
  }

  int
  CertStore :: get_all_certs_from_disk(std::set<X509*>& setme)
  {

    int cnt(0);
    quarks_t servers(_data.get_servers());
    foreach_const(quarks_t, servers, it)
    {
      const Data::Server* s(_data.find_server(*it));
      if (!s) continue;
      const char* filename(s->cert.c_str());
      if (!filename) continue;
      FILE *fp = fopen(filename,"r");
      if (!fp) continue;
      X509 *x = X509_new();
      if (!x) { fclose(fp); continue; }
      PEM_read_X509(fp,&x, 0, 0);
      fclose(fp);
      setme.insert(x);
      _certs.insert(*it);
      _cert_to_server[*it] = x;
      ++cnt;
    }

    return cnt;
  }

  void
  CertStore :: init_me()
  {
    assert (_ctx);

    _store = SSL_CTX_get_cert_store(_ctx);

    std::set<X509*> certs;
    int r(0);
    get_all_certs_from_disk (certs);
    foreach_const (std::set<X509*>, certs, it)
      if (X509_STORE_add_cert(_store, *it) != 0) ++r;
    if (r != 0) Log::add_info_va(_("Succesfully added %d SSL PEM certificate(s) to Certificate Store."), r);
    SSL_CTX_set_verify(_ctx, SSL_VERIFY_PEER|SSL_VERIFY_FAIL_IF_NO_PEER_CERT, verify_callback);

  }

  void
  CertStore :: remove_hard(const Quark& server)
  {
    char buf[2048];
    g_snprintf (buf, sizeof(buf), "%s%c%s.pem", _path.c_str(), G_DIR_SEPARATOR, server.c_str());
    unlink(buf);
  }

  void
  CertStore :: remove (const Quark& server)
  {
    debug("remove cert "<<server);
    if (_cert_to_server.count(server))
    {
      _cert_to_server.erase(server);
      _certs.erase(server);
      remove_hard(server);
      Quark setme;
      _data.set_server_cert(server, "");
      _data.save_server_info(server);
    }

  }

  CertStore :: CertStore (Data& data): _data(data)
  {
    char buf[2048];
    g_snprintf(buf,sizeof(buf),"%s%cssl_certs",file::get_pan_home().c_str(), G_DIR_SEPARATOR);
    _path = buf;
    if (!file::ensure_dir_exists (buf))
    {
      std::cerr<<"Error initializing certstore. Check your permissions for the directory \"ssl-certs\" and main subfolder in your home directory! Fatal, exiting.";
      file::print_file_info(std::cerr, buf);
      exit(EXIT_FAILURE);
    }
  }

  CertStore :: ~CertStore ()
  {}



  bool
  CertStore :: add(X509* cert, const Quark& server)
  {
    debug(server<<" "<<cert);
    if (!cert || server.empty()) return false;
    debug(X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0));
    X509_STORE_add_cert(get_store(),cert);

    std::string addr; int port;
    _data.get_server_addr(server, addr, port);
    _certs.insert(server);
    _cert_to_server[server] = cert;

    debug(server<<" "<<addr);

    const char* buf(build_cert_name(addr.c_str()).c_str());
    _data.set_server_cert(server, buf);
    _data.save_server_info(server);

    FILE * fp = fopen(buf, "wb");
    if (!fp) return false;
    if (!PEM_write_X509(fp, cert)) { fclose(fp); return false; }
    fclose(fp);
    chmod (buf, 0600);

    debug(server<<" "<<buf);

    valid_cert_added(cert, server.c_str());
    return true;
  }


  X509*
  CertStore :: get_cert_to_server(const Quark& server) const
  {
    X509* ret(0);
    Quark serv;

    /* strip port from server if existing */
    std::string s(server);
    std::string::size_type idx = s.rfind(":");
    if(idx != std::string::npos)
      serv = s.substr(0,idx);
    else
      serv = server;

    if (_cert_to_server.count(serv))
      ret = _cert_to_server.find(serv)->second;
    return ret;
  }

  std::string
  CertStore :: build_cert_name(std::string host)
  {
    char buf[2048];
    g_snprintf(buf,sizeof(buf),"%s%cssl_certs%c%s.pem",file::get_pan_home().c_str(),
               G_DIR_SEPARATOR,G_DIR_SEPARATOR, host.c_str());
    return buf;
  }


}  // namespace pan


#endif
