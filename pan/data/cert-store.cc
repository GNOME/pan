/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2011 Heinrich M�ller <sphemuel@stud.informatik.uni-erlangen.de>
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

//#include <glib/giochannel.h>
//#include <glib/gstring.h>

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
  #include <glib.h>
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

#ifdef HAVE_GNUTLS

namespace pan
{

  int
  verify_callback(gnutls_session_t session)
  {

    mydata_t* mydata = (mydata_t*)gnutls_session_get_ptr (session);

    unsigned int status;
    const gnutls_datum_t *cert_list;
    unsigned int cert_list_size;
    int ret;
    gnutls_x509_crt_t cert;
    bool fail(false);

    ret = gnutls_certificate_verify_peers2 (session, &status);

    if (ret < 0)
      return GNUTLS_E_CERTIFICATE_ERROR;

    if (status & GNUTLS_CERT_INVALID)
    {
      g_warning ("The certificate is not trusted.\n");
      fail = !mydata->always_trust;
    }

    if (status & GNUTLS_CERT_SIGNER_NOT_FOUND)
    {
      fail = !mydata->always_trust;
      g_warning ("The certificate hasn't got a known issuer.\n");
    }

    if (status & GNUTLS_CERT_REVOKED)
    {
      g_warning ("The certificate has been revoked.\n");
      fail = true;
    }

    if (status & GNUTLS_CERT_EXPIRED)
    {
      g_warning ("The certificate has expired\n");
      fail = true;
    }

    if (status & GNUTLS_CERT_NOT_ACTIVATED)
    {
      g_warning ("The certificate is not yet activated\n");
      fail = true;
    }

    /* Up to here the process is the same for X.509 certificates and
     * OpenPGP keys. From now on X.509 certificates are assumed. This can
     * be easily extended to work with openpgp keys as well.
     */
    if (gnutls_certificate_type_get (session) != GNUTLS_CRT_X509)
    {
      g_warning ("The certificate is not a X509 certificate!\n");
      goto _fail;
    }

    if (gnutls_x509_crt_init (&cert) < 0)
    {
      g_error ("Error in initialization\n");
      goto _fail;
    }

    cert_list = gnutls_certificate_get_peers (session, &cert_list_size);
    if (cert_list == NULL)
    {
      g_error ("No certificate found!\n");
      goto _fail;
    }

    /* TODO verify whole chain perhaps?
     */
    if (gnutls_x509_crt_import (cert, &cert_list[0], GNUTLS_X509_FMT_DER) < 0)
    {
      g_error ("Error parsing certificate!\n");
      goto _fail;
    }

    if (!gnutls_x509_crt_check_hostname (cert, mydata->hostname_full.c_str()))
    {
      g_error ("The certificate's owner does not match hostname '%s' !\n", mydata->hostname_full.c_str());
      goto _fail;
    }

    if (fail) goto _fail;

    /* auto-add new cert if we always trust this server */
    if (mydata->always_trust)
      mydata->cs->add(cert, mydata->host);
    else
      gnutls_x509_crt_deinit(cert);

    /* notify gnutls to continue handshake normally */
    return 0;

    _fail:

    if (cert)
      mydata->cs->verify_failed (cert, mydata->host.c_str(), status);

    return GNUTLS_E_CERTIFICATE_ERROR;

  }

  bool
  CertStore :: import_from_file (const Quark& server, const char* fn)
  {

    size_t filelen;
    char * buf;

    Data::Server* s(_data.find_server(server));
    if (!s) return false;

    const char* filename(fn ? fn : s->cert.c_str());
    if (!filename) return false;

    FILE * fp = fopen(filename, "rb");
    if (!fp) return false;

    fseek (fp, 0, SEEK_END);
    filelen = ftell (fp);
    fseek (fp, 0, SEEK_SET);
    buf = new char[filelen];
    fread (buf, sizeof(char), filelen, fp);

    gnutls_datum_t in;
    in.data = (unsigned char*)buf;
    in.size = filelen;
    gnutls_x509_crt_t cert;
    gnutls_x509_crt_init(&cert);
    gnutls_x509_crt_import(cert, &in, GNUTLS_X509_FMT_PEM);

    delete buf;

    int ret = gnutls_certificate_set_x509_trust(_creds, &cert, 1);

    if (ret < 0) goto fail;

    _cert_to_server[server] = cert;

    return true;

    fail:
      s->cert.clear();
      gnutls_x509_crt_deinit (cert);
      _data.save_server_info(server);

    return false;

  }

  int
  CertStore :: get_all_certs_from_disk()
  {

    int cnt(0);
    quarks_t servers(_data.get_servers());
    int ret(0);
    size_t filelen;
    char * buf;
    GError* err(NULL);

    foreach_const(quarks_t, servers, it)
      if (import_from_file(*it)) ++cnt;

    // get certs from ssl certs directory
    char * ssldir(0);
    ssldir = getenv("SSL_CERT_DIR");
    if (!ssldir) ssldir = getenv("SSL_DIR");
    if (!ssldir) return cnt;

    GDir * dir = g_dir_open (ssldir, 0, &err);
    if (err != NULL)
    {
      Log::add_err_va (_("Error opening SSL certificate directory: \"%s\": %s"), ssldir, err->message);
      g_error_free (err);
    }
    else
    {
      char filename[PATH_MAX];
      const char * fname;
      struct stat stat_p;
      while ((fname = g_dir_read_name (dir)))
      {
        struct stat stat_p;
        g_snprintf (filename, sizeof(filename), "%s%c%s", ssldir, G_DIR_SEPARATOR, fname);
        if (!stat (filename, &stat_p))
        {
          if (!S_ISREG(stat_p.st_mode)) continue;
          ret = gnutls_certificate_set_x509_trust_file(_creds, filename, GNUTLS_X509_FMT_PEM);
          if (ret > 0) cnt += ret;
        }
      }
      g_dir_close (dir);
    }

    return cnt;
  }


  void
  CertStore :: init()
  {
    int r(0);
    r = get_all_certs_from_disk ();

    if (r != 0) Log::add_info_va(_("Succesfully added %d SSL PEM certificate(s) to Certificate Store."), r);

  }

  void
  CertStore :: remove_hard(const Quark& server)
  {
    char buf[2048];
    std::string fn = _data.get_server_cert(server);
    std::cerr<<"unlink "<<fn<<"\n";
    unlink(fn.c_str());
  }

  void
  CertStore :: remove (const Quark& server)
  {
    _cert_to_server.erase(server);
    remove_hard (server);
  }

  CertStore :: CertStore (Data& data): _data(data)
  {
    char buf[2048];
    g_snprintf(buf,sizeof(buf),"%s%cssl_certs",file::get_pan_home().c_str(), G_DIR_SEPARATOR);
    _path = buf;
    if (!file::ensure_dir_exists (buf))
    {
      std::cerr<<"Error initializing certstore. Check your permissions for the pan2 subfolder \"ssl-certs\" and "
                 "the pan2 folder in your Home directory! Fatal, exiting.";
      file::print_file_info(std::cerr, buf);
      exit(EXIT_FAILURE);
    }

    gnutls_certificate_allocate_credentials (&_creds);
    gnutls_certificate_set_verify_function(_creds, verify_callback);

  }

  CertStore :: ~CertStore ()
  {
    gnutls_certificate_free_credentials (_creds);
    foreach (certs_m, _cert_to_server, it)
      gnutls_x509_crt_deinit(it->second);
  }



  bool
  CertStore :: add (gnutls_x509_crt_t cert, const Quark& server)
  {
    debug("adding server cert "<<server<<" "<<cert);
    if (!cert || server.empty()) return false;

    std::string addr; int port;
    _data.get_server_addr(server, addr, port);
    _cert_to_server[server] = cert;

    const char* buf(build_cert_name(addr).c_str());

    _data.set_server_cert(server, buf);

    _data.save_server_info(server);

    FILE * fp = fopen(buf, "wb");
    if (!fp) return false;
    size_t outsize;

    /* make up for dumbness of this function */
    gnutls_x509_crt_export (cert, GNUTLS_X509_FMT_PEM, NULL, &outsize);
    char* out = new char[outsize];
    gnutls_x509_crt_export (cert, GNUTLS_X509_FMT_PEM, out, &outsize);

    fputs ((const char*)out, fp);

    fclose(fp);

    delete out;

    chmod (buf, 0600);

    gnutls_certificate_set_x509_trust(_creds, &cert, 1); // for now, only 1 is saved
    valid_cert_added(cert, server.c_str());

    return true;
  }


  std::string
  CertStore :: build_cert_name(std::string& host)
  {
    char buf[2048];
    g_snprintf(buf,sizeof(buf),"%s%cssl_certs%c%s.pem",file::get_pan_home().c_str(),
               G_DIR_SEPARATOR,G_DIR_SEPARATOR, host.c_str());
    return buf;
  }


}  // namespace pan


#endif
