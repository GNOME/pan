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
      if (mydata->ignore_all==1) return 1;

      X509 *cert = X509_STORE_CTX_get_current_cert(store);
      int depth = X509_STORE_CTX_get_error_depth(store);
      int err = X509_STORE_CTX_get_error(store);

//      std::cerr<<"ssl verify err "<<err<<" "<<ok<<std::endl;

      /* accept user-override on self-signed certificates */
      if (err == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN ||
          err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
          err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT ||
          err == X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
        mydata->cs->verify_failed(cert, mydata->server, err);
      else
        g_warning("[[DEBUG:]] unknown error condition, please report me: %s", ssl_err_to_string(err).c_str());
    }
    return ok;
}

int
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
  if (_cert_to_server.count(server) > 0)
  {
    _cert_to_server.erase(server);
    _certs.erase(server);
    remove_hard(server);
  }
}

CertStore :: CertStore ()
{
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
  chmod (buf, 0600);

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
			res = _("Not available.");
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
				res = fp;
			}
		}
    return res;
  }

  std::string
  get_x509_enddate(X509* cert)
  {
    std::string res;

    return res;
  }

}

time_t
getTimeFromASN1(const ASN1_TIME * aTime)
{
  time_t lResult = 0;


  char lBuffer[24];
  char * pBuffer = lBuffer;


  size_t lTimeLength = aTime->length;
  char * pString = (char *)aTime->data;


  if (aTime->type == V_ASN1_UTCTIME)
  {
    if ((lTimeLength < 11) || (lTimeLength > 17))
    {
      return 0;
    }


    memcpy(pBuffer, pString, 10);
    pBuffer += 10;
    pString += 10;
  }
  else
  {
    if (lTimeLength < 13)
    {
      return 0;
    }


    memcpy(pBuffer, pString, 12);
    pBuffer += 12;
    pString += 12;
  }


  if ((*pString == 'Z') || (*pString == '-') || (*pString == '+'))
  {
    *(pBuffer++) = '0';
    *(pBuffer++) = '0';
  }
  else
  {
    *(pBuffer++) = *(pString++);
    *(pBuffer++) = *(pString++);
    // Skip any fractional seconds...
    if (*pString == '.')
    {
      pString++;
      while ((*pString >= '0') && (*pString <= '9'))
      {
        pString++;
      }
    }
  }


  *(pBuffer++) = 'Z';
  *(pBuffer++) = '\0';


  time_t lSecondsFromUCT;
  if (*pString == 'Z')
  {
    lSecondsFromUCT = 0;
  }
  else
  {
    if ((*pString != '+') && (pString[5] != '-'))
    {
      return 0;
    }


    lSecondsFromUCT = ((pString[1]-'0') * 10 + (pString[2]-'0')) * 60;
    lSecondsFromUCT += (pString[3]-'0') * 10 + (pString[4]-'0');
    if (*pString == '-')
    {
      lSecondsFromUCT = -lSecondsFromUCT;
    }
  }


  tm lTime;
  lTime.tm_sec  = ((lBuffer[10] - '0') * 10) + (lBuffer[11] - '0');
  lTime.tm_min  = ((lBuffer[8] - '0') * 10) + (lBuffer[9] - '0');
  lTime.tm_hour = ((lBuffer[6] - '0') * 10) + (lBuffer[7] - '0');
  lTime.tm_mday = ((lBuffer[4] - '0') * 10) + (lBuffer[5] - '0');
  lTime.tm_mon  = (((lBuffer[2] - '0') * 10) + (lBuffer[3] - '0')) - 1;
  lTime.tm_year = ((lBuffer[0] - '0') * 10) + (lBuffer[1] - '0');
  if (lTime.tm_year < 50)
  {
    lTime.tm_year += 100; // RFC 2459
  }
  lTime.tm_wday = 0;
  lTime.tm_yday = 0;
  lTime.tm_isdst = 0;  // No DST adjustment requested

  lResult = mktime(&lTime);

  if ((time_t)-1 != lResult)
  {
    if (0 != lTime.tm_isdst)
      lResult -= 3600; // mktime may adjust for DST (OS dependent)
    lResult += lSecondsFromUCT;
  }
  else
    lResult = 0;

  return lResult;
}

void
CertStore :: pretty_print_x509 (char* buf, size_t size, const Quark& server, X509* cert)
{
  if (!cert) return;
//  char buffer[4096];
//  int len;
//  int i = X509_get_ext_by_NID(cert, NID_invalidity_date, -1);
//  BIO *bio = BIO_new(BIO_s_mem());
//  X509_EXTENSION * ex = X509_get_ext( cert,i);
//  std::cerr<<"pretty "<<i<<" "<<ex<<" "<<bio<<std::endl;
//  if(!X509V3_EXT_print(bio, ex, 0, 0))
//    M_ASN1_OCTET_STRING_print(bio,ex->value);
//  len = BIO_read(bio, buffer, 4096);
//  buffer[len] = '\0';
//  BIO_free(bio);

  time_t t = getTimeFromASN1(cert->cert_info->validity->notAfter);
  time_t t2 = getTimeFromASN1(cert->cert_info->validity->notBefore);
  EvolutionDateMaker date_maker;
  char * until = date_maker.get_date_string (t);
  char * before = date_maker.get_date_string (t2);

  g_snprintf(buf,size, _("The current server <b>'%s'</b> sent this security certificate :\n\n"
                              "<b>Issuer :</b>\n%s\n\n"
                              "<b>Subject : </b>\n%s\n\n"
                              "<b>Valid until : </b>%s\n\n"
                              "<b>Not valid before : </b>%s\n\n"
                              "<b>Fingerprint (MD5) : </b>\n%s\n\n"),
                              server.c_str(),
                              X509_NAME_oneline(cert->cert_info->issuer, 0, 0),
                              X509_NAME_oneline(cert->cert_info->subject, 0, 0),
                              until,
                              before,
                              get_x509_fingerpint_md5(cert).c_str());

}



}  // namespace pan


#endif
