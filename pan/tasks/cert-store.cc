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
#include <cerrno>
#include <cstring>

extern "C" {
  #include <glib/gi18n.h>
  #include <dirent.h>
}

#include <pan/general/debug.h>
#include <pan/general/e-util.h>
#include <pan/general/macros.h>
#include <pan/usenet-utils/mime-utils.h>

#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/log.h>
#include <pan/general/string-view.h>
#include <pan/usenet-utils/mime-utils.h>

#include "cert-store.h"

using namespace pan;

namespace pan
{

  int
  verify_callback(int ok, X509_STORE_CTX *store)
//  verify_callback(X509_STORE_CTX *store, void* args)
  {

    SSL * ssl = (SSL*)X509_STORE_CTX_get_ex_data(store, SSL_get_ex_data_X509_STORE_CTX_idx());
    mydata_t* mydata = (mydata_t*)SSL_get_ex_data(ssl, SSL_get_fd(ssl));

//    CertStore * me = (CertStore*)args;

    if (!ok)
    {
      if (mydata->ignore_all==1) return 1;

      X509 *cert = X509_STORE_CTX_get_current_cert(store);
      int depth = X509_STORE_CTX_get_error_depth(store);
      int err = X509_STORE_CTX_get_error(store);

      if (err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN)
        mydata->cs->verify_failed(cert, mydata->server, err);
    }
    return ok;

//      SSL_CTX_set_verify(me->get_ctx(), SSL_VERIFY_PEER, store->verify_cb);
//
//      int ok =
//
//  }

}

void
CertStore :: get_all_certs_from_disk(std::set<X509*>& setme)
{
  char filename[PATH_MAX];
  const char * fname;
  GError * err = NULL;
  GDir * dir = g_dir_open (_path.c_str(), 0, &err);

  int cnt(0);
  while ((fname = g_dir_read_name (dir)))
  {
    if (strlen(fname)<=1) continue;

    g_snprintf (filename, sizeof(filename), "%s%c%s", _path.c_str(), G_DIR_SEPARATOR, fname);
    FILE *fp = fopen(filename,"r");
    X509 *x = X509_new();
    PEM_read_X509(fp,&x, 0, 0);
    fclose(fp);
    setme.insert(x);

    std::string fn(fname);
    std::string::size_type idx = fn.rfind(".pem");

    if(idx != std::string::npos)
    {
      std::string server = fn.substr(0,idx);
      _certs.insert(server);
      _cert_to_server[server] = x;
    }
    ++cnt;
  }
  g_dir_close (dir);

  if (cnt != 0) Log::add_info_va(_("Succesfully added %d SSL PEM certificate(s) to Certificate Store."), cnt);
}

void
CertStore :: init_me()
{
  assert (_ctx);

  _store = SSL_CTX_get_cert_store(_ctx);

  std::set<X509*> certs;
  get_all_certs_from_disk (certs);
  foreach_const (std::set<X509*>, certs, it)
    X509_STORE_add_cert(_store, *it);
//  SSL_CTX_set_cert_verify_callback(_ctx, verify_callback, (void*)this);
  SSL_CTX_set_verify(_ctx, SSL_VERIFY_PEER, verify_callback);

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
  if (_cert_to_server.count(server) > 0)
  {
    _cert_to_server.erase(server);
    _certs.erase(server);
    remove_hard(server);
  }
}

CertStore :: CertStore (SSL_CTX * ctx) : _ctx(ctx)
{
  if (ctx) init_me();
  char buf[2048];
  g_snprintf(buf,sizeof(buf),"%s%cssl_certs",file::get_pan_home().c_str(), G_DIR_SEPARATOR);
  file::ensure_dir_exists (buf);
  _path = buf;
}

CertStore :: ~CertStore ()
{}



bool
CertStore :: add(X509* cert, const Quark& server)
{
  if (_certs.count(server) > 0 || !cert || server.empty()) return false;

  X509_STORE_add_cert(get_store(),cert);
  _certs.insert(server);
  _cert_to_server[server] = cert;

  char buf[2048];
  g_snprintf(buf,sizeof(buf),"%s%c%s.pem",_path.c_str(),G_DIR_SEPARATOR,server.c_str());
  FILE * fp = fopen(buf, "wb");
  PEM_write_X509(fp, cert);
  fclose(fp);

  valid_cert_added(cert, server.c_str());
  return true;
}

const X509*
CertStore :: get_cert_to_server(const Quark& server) const
{
  const X509* ret(0);
  Quark serv;

  /* strip port from server if existing */
  std::string s(server);
  std::string::size_type idx = s.rfind(":");
  if(idx != std::string::npos)
    serv = s.substr(0,idx);
  else
    serv = server;

  /* dbg dump all */
//  std::cerr<<"asking for server cert "<<serv<<std::endl;
//  std::cerr<<"existing certs : \n";
//  foreach_const(certs_m, _cert_to_server, it)
//  {
//    std::cerr<<it->first<<" "<<it->second<<std::endl;
//  }

  if (_cert_to_server.count(serv) > 0)
    ret = _cert_to_server.find(serv)->second;
  return ret;
}

namespace
{
  std::string
  get_x509_fingerpint_md5(X509* cert)
  {
    std::string res;
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int n;

    if (! X509_digest(cert, EVP_md5(), md, &n))
			res += _("Not available.");
		else {
			char hex[] = "0123456789ABCDEF";
			char fp[EVP_MAX_MD_SIZE*3];
			if (n < sizeof(fp)) {
				unsigned int i;
				for (i = 0; i < n; i++) {
					fp[i*3+0] = hex[(md[i] >> 4) & 0xF];
					fp[i*3+1] = hex[(md[i] >> 0) & 0xF];
					fp[i*3+2] = i == n - 1 ? '\0' : ':';
				}
				res += fp;
			}
		}
    return res;
  }

}

void
CertStore :: pretty_print_x509 (char* buf, size_t size, const Quark& server, X509* cert)
{
  if (!cert) return;
  g_snprintf(buf,size, _("The current server <b>'%s'</b> sent this security certificate :\n\n"
                              "<b>Issuer :</b> \n%s\n\n"
                              "<b>Subject : </b>\n%s\n\n"
                              "<b>Fingerprint : </b>\n%s\n\n"),
                              server.c_str(),
                              X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0),
                              X509_NAME_oneline(X509_get_subject_name(cert), 0, 0),
                              get_x509_fingerpint_md5(cert).c_str());
}



}  // namespace pan
