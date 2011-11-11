
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

/** Copyright notice: Some code taken from here :
  * http://dslinux.gits.kiev.ua/trunk/user/irssi/src/src/core/network-openssl.c
  * Copyright (C) 2002 vjt (irssi project) */

#ifndef _SSL_UTILS_H_
#define _SSL_UTILS_H_

#ifdef HAVE_OPENSSL

#include <pan/tasks/socket.h>
#include <pan/general/quark.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include <pan/tasks/socket.h>
#include <pan/general/e-util.h>
#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <map>
#include <iostream>
#include <sstream>
extern "C" {
  #include <glib/gi18n.h>
}

namespace pan
{

  /* Checks if the given string has internal NUL characters. */
  static gboolean has_internal_nul(const char* str, int len) {
    /* Remove trailing nul characters. They would give false alarms */
    while (len > 0 && str[len-1] == 0)
      len--;
    return strlen(str) != len;
  }

  /* tls_dns_name - Extract valid DNS name from subjectAltName value */
  static const char *tls_dns_name(const GENERAL_NAME * gn)
  {
    const char *dnsname;

    /* We expect the OpenSSL library to construct GEN_DNS extension objects as
       ASN1_IA5STRING values. Check we got the right union member. */
    if (ASN1_STRING_type(gn->d.ia5) != V_ASN1_IA5STRING) {
      g_warning("Invalid ASN1 value type in subjectAltName");
      return NULL;
    }

    /* Safe to treat as an ASCII string possibly holding a DNS name */
    dnsname = (char *) ASN1_STRING_data(gn->d.ia5);

    if (has_internal_nul(dnsname, ASN1_STRING_length(gn->d.ia5))) {
      g_warning("Internal NUL in subjectAltName");
      return NULL;
    }

    return dnsname;
  }

  /* tls_text_name - extract certificate property value by name */
  static char *tls_text_name(X509_NAME *name, int nid)
  {
    int     pos;
    X509_NAME_ENTRY *entry;
    ASN1_STRING *entry_str;
    int     utf8_length;
    unsigned char *utf8_value;
    char *result;

    if (name == 0 || (pos = X509_NAME_get_index_by_NID(name, nid, -1)) < 0) {
      return NULL;
      }

      entry = X509_NAME_get_entry(name, pos);
      g_return_val_if_fail(entry != NULL, NULL);
      entry_str = X509_NAME_ENTRY_get_data(entry);
      g_return_val_if_fail(entry_str != NULL, NULL);

      /* Convert everything into UTF-8. It's up to OpenSSL to do something
       reasonable when converting ASCII formats that contain non-ASCII
       content. */
      if ((utf8_length = ASN1_STRING_to_UTF8(&utf8_value, entry_str)) < 0) {
        g_warning("Error decoding ASN.1 type=%d", ASN1_STRING_type(entry_str));
        return NULL;
      }

      if (has_internal_nul((char *)utf8_value, utf8_length)) {
        g_warning("NUL character in hostname in certificate");
        OPENSSL_free(utf8_value);
        return NULL;
      }

      result = g_strdup((char *) utf8_value);
    OPENSSL_free(utf8_value);
    return result;
  }


  /** check if a hostname in the certificate matches the hostname we used for the connection */
  static gboolean match_hostname(const char *cert_hostname, const char *hostname)
  {
    const char *hostname_left;

    if (!strcasecmp(cert_hostname, hostname)) { /* exact match */
      return TRUE;
    } else if (cert_hostname[0] == '*' && cert_hostname[1] == '.' && cert_hostname[2] != 0) { /* wildcard match */
      /* The initial '*' matches exactly one hostname component */
      hostname_left = strchr(hostname, '.');
      if (hostname_left != NULL && ! strcasecmp(hostname_left + 1, cert_hostname + 2)) {
        return TRUE;
      }
    }
    return FALSE;
  }

  static gboolean ssl_verify_hostname(X509 *cert, const char *hostname)
  {
    int gen_index, gen_count;
    gboolean matched = FALSE, has_dns_name = FALSE;
    const char *cert_dns_name;
    char *cert_subject_cn;
    const GENERAL_NAME *gn;
    STACK_OF(GENERAL_NAME) * gens;

    /* Verify the dNSName(s) in the peer certificate against the hostname. */
    gens = (STACK_OF(GENERAL_NAME) *) X509_get_ext_d2i(cert, NID_subject_alt_name, 0, 0);
    if (gens) {
      gen_count = sk_GENERAL_NAME_num(gens);
      for (gen_index = 0; gen_index < gen_count && !matched; ++gen_index) {
        gn = sk_GENERAL_NAME_value(gens, gen_index);
        if (gn->type != GEN_DNS)
          continue;

        /* Even if we have an invalid DNS name, we still ultimately
           ignore the CommonName, because subjectAltName:DNS is
           present (though malformed). */
        has_dns_name = TRUE;
        cert_dns_name = tls_dns_name(gn);
        if (cert_dns_name && *cert_dns_name) {
          matched = match_hostname(cert_dns_name, hostname);
        }
        }

        /* Free stack *and* member GENERAL_NAME objects */
        sk_GENERAL_NAME_pop_free(gens, GENERAL_NAME_free);
    }

    if (has_dns_name) {
      if (! matched) {
        /* The CommonName in the issuer DN is obsolete when SubjectAltName is available. */
        g_warning("None of the Subject Alt Names in the certificate match hostname '%s'", hostname);
      }
      return matched;
    } else { /* No subjectAltNames, look at CommonName */
      cert_subject_cn = tls_text_name(X509_get_subject_name(cert), NID_commonName);
        if (cert_subject_cn && *cert_subject_cn) {
          matched = match_hostname(cert_subject_cn, hostname);
          if (! matched) {
          g_warning("SSL certificate common name '%s' doesn't match host name '%s'", cert_subject_cn, hostname);
          }
        } else {
          g_warning("No subjectAltNames and no valid common name in certificate");
        }
        free(cert_subject_cn);
    }

    return matched;
  }

  static gboolean ssl_verify(SSL *ssl, SSL_CTX *ctx, const char* hostname, X509 *cert)
  {
    long result;

    result = SSL_get_verify_result(ssl);
    if (result != X509_V_OK) {
      unsigned char md[EVP_MAX_MD_SIZE];
      unsigned int n;
      char *str;

      g_warning("Could not verify SSL servers certificate: %s",
          X509_verify_cert_error_string(result));
      if ((str = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0)) == NULL)
        g_warning("  Could not get subject-name from peer certificate");
      else {
        g_warning("  Subject : %s", str);
        free(str);
      }
      if ((str = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0)) == NULL)
        g_warning("  Could not get issuer-name from peer certificate");
      else {
        g_warning("  Issuer  : %s", str);
        free(str);
      }
      if (! X509_digest(cert, EVP_md5(), md, &n))
        g_warning("  Could not get fingerprint from peer certificate");
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
          g_warning("  MD5 Fingerprint : %s", fp);
        }
      }
      return FALSE;
    } else if (! ssl_verify_hostname(cert, hostname)){
      return FALSE;
    }
    return TRUE;
  }

  static std::map<int, Quark> ssl_err;
  static int map_init(0);
  typedef std::pair<int, Quark> err_p;

  static void init_err_map()
  {
    ssl_err.insert(err_p(2,"X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT"));
    ssl_err.insert(err_p(3,"X509_V_ERR_UNABLE_TO_GET_CRL"));
    ssl_err.insert(err_p(4,"X509_V_ERR_UNABLE_TO_DECRYPT_CERT_SIGNATURE"));
    ssl_err.insert(err_p(5,"X509_V_ERR_UNABLE_TO_DECRYPT_CRL_SIGNATURE"));
    ssl_err.insert(err_p(6,"X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY"));
    ssl_err.insert(err_p(7,"X509_V_ERR_CERT_SIGNATURE_FAILURE"));
    ssl_err.insert(err_p(8,"X509_V_ERR_CRL_SIGNATURE_FAILURE"));
    ssl_err.insert(err_p(9,"X509_V_ERR_CERT_NOT_YET_VALID"));
    ssl_err.insert(err_p(10,"X509_V_ERR_CERT_HAS_EXPIRED"));
    ssl_err.insert(err_p(11,"X509_V_ERR_CRL_NOT_YET_VALID"));
    ssl_err.insert(err_p(12,"X509_V_ERR_CRL_HAS_EXPIRED"));
    ssl_err.insert(err_p(13,"X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD"));
    ssl_err.insert(err_p(14,"X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD"));
    ssl_err.insert(err_p(15,"X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD"));
    ssl_err.insert(err_p(16,"X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD"));
    ssl_err.insert(err_p(17,"X509_V_ERR_OUT_OF_MEM"));
    ssl_err.insert(err_p(18,"X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT"));
    ssl_err.insert(err_p(19,"X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN"));
    ssl_err.insert(err_p(20,"X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY "));
    ssl_err.insert(err_p(21,"X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE	"));
    ssl_err.insert(err_p(22,"X509_V_ERR_CERT_CHAIN_TOO_LONG"));
    ssl_err.insert(err_p(23,"X509_V_ERR_CERT_REVOKED"));
    ssl_err.insert(err_p(24,"X509_V_ERR_INVALID_CA"));
    ssl_err.insert(err_p(25,"X509_V_ERR_PATH_LENGTH_EXCEEDED"));
    ssl_err.insert(err_p(26,"X509_V_ERR_INVALID_PURPOSE"));
    ssl_err.insert(err_p(27,"X509_V_ERR_CERT_UNTRUSTED"));
    ssl_err.insert(err_p(28,"X509_V_ERR_CERT_REJECTED"));
  }

  static const Quark
  ssl_err_to_string(int i)
  {
    if (map_init++ == 0) init_err_map();
    Quark ret;
    if (ssl_err.count(i) > 0) return ssl_err[i];
    return ret;
  }

  static time_t
  getTimeFromASN1(const ASN1_TIME * aTime)
  {
    time_t lResult = 0;
    char lBuffer[24];
    char * pBuffer = lBuffer;
    size_t lTimeLength = aTime->length;
    char * pString = (char *)aTime->data;

    if (aTime->type == V_ASN1_UTCTIME)
    {
      if ((lTimeLength < 11) || (lTimeLength > 17)) return 0;
      memcpy(pBuffer, pString, 10);
      pBuffer += 10;
      pString += 10;
    }
    else
    {
      if (lTimeLength < 13) return 0;
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
          pString++;
      }
    }

    *(pBuffer++) = 'Z';
    *(pBuffer++) = '\0';

    time_t lSecondsFromUCT;
    if (*pString == 'Z')
      lSecondsFromUCT = 0;
    else
    {
      if ((*pString != '+') && (pString[5] != '-')) return 0;
      lSecondsFromUCT = ((pString[1]-'0') * 10 + (pString[2]-'0')) * 60;
      lSecondsFromUCT += (pString[3]-'0') * 10 + (pString[4]-'0');
      if (*pString == '-')
        lSecondsFromUCT = -lSecondsFromUCT;
    }

    tm lTime;
    lTime.tm_sec  = ((lBuffer[10] - '0') * 10) + (lBuffer[11] - '0');
    lTime.tm_min  = ((lBuffer[8] - '0') * 10) + (lBuffer[9] - '0');
    lTime.tm_hour = ((lBuffer[6] - '0') * 10) + (lBuffer[7] - '0');
    lTime.tm_mday = ((lBuffer[4] - '0') * 10) + (lBuffer[5] - '0');
    lTime.tm_mon  = (((lBuffer[2] - '0') * 10) + (lBuffer[3] - '0')) - 1;
    lTime.tm_year = ((lBuffer[0] - '0') * 10) + (lBuffer[1] - '0');
    if (lTime.tm_year < 50)
      lTime.tm_year += 100; // RFC 2459
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


  static std::string
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

  typedef std::pair<Quark,Quark> quarks_p;
  typedef std::map<Quark,Quark>::iterator tags_it;

  const static char* tags_idx[] =
  {
    "/L=",
    "/CN=",
    "/C=",
    "/OU=",
    "/O=",
    "/ST=",
    "/EMAIL=",
    "/emailAddress="
  };

  const static char* cleaned_tags[] =
  {
    "L", "CN", "C", "OU", "O", "ST", "EMAIL", "emailAdress"
  };

  struct CertParser
  {
    X509* cert;
    std::map<Quark, Quark> tags;
    char* issuer, * subject;
    std::string iss, sub;
    int pos1, pos2, tmp_pos1, idx;
    char buf[256];
    size_t num_tags;
    const char delim;

    CertParser(X509* c) : cert(c), delim('/'), pos1(0), pos2(0), idx(0), num_tags(G_N_ELEMENTS(tags_idx))
    {
      issuer  = X509_NAME_oneline(cert->cert_info->issuer,0,0);
      subject = X509_NAME_oneline(cert->cert_info->subject, 0, 0);
      iss = issuer;
      sub = subject;
      /* init map */
      int i(0);
      tags.insert(quarks_p(cleaned_tags[i++],"Locality"));
      tags.insert(quarks_p(cleaned_tags[i++],"Common Name"));
      tags.insert(quarks_p(cleaned_tags[i++],"Company"));
      tags.insert(quarks_p(cleaned_tags[i++],"Organizational Unit"));
      tags.insert(quarks_p(cleaned_tags[i++],"Organization"));
      tags.insert(quarks_p(cleaned_tags[i++],"State"));
      tags.insert(quarks_p(cleaned_tags[i++],"Email Address"));
      tags.insert(quarks_p(cleaned_tags[i],"Email Address"));
    }

    void parse(std::vector<quarks_p>& i, std::vector<quarks_p>& s)
    {
      while(idx<num_tags)
      {
        std::string::size_type index = iss.find(tags_idx[idx]);
        if(index != std::string::npos)
        {
          pos1 = (int)index + strlen(tags_idx[idx]);
          pos2 = (int)iss.find(delim, pos1);
          if (pos2<=0) goto _end;
          if (!strcmp(cleaned_tags[idx],"CN"))
          {
            int tmp_pos = (int)iss.find("://",pos2-1);
            if (tmp_pos == pos2-1)
              pos2 = (int)iss.find(delim, pos2+2);
          }
          std::string tmp = iss.substr(pos1,pos2-pos1);
          g_snprintf(buf, sizeof(buf), "%s (%s)", cleaned_tags[idx], tags[cleaned_tags[idx]].c_str() );
          i.push_back(quarks_p(buf, tmp));
        }
        _end:
          ++idx;
      }

      idx = 0;
      while(idx<num_tags)
      {
        std::string::size_type index = sub.find(tags_idx[idx]);
        if(index != std::string::npos)
        {
          pos1 = (int)index + strlen(tags_idx[idx]);
          pos2 = (int)iss.find(delim, pos1);
          if (pos2<=0) goto _end2;
          if (!strcmp(cleaned_tags[idx],"CN"))
          {
            int tmp_pos = (int)iss.find("://",pos2-1);
            if (tmp_pos == pos2-1)
              pos2 = (int)iss.find(delim, pos2+2);
          }
          std::string tmp = sub.substr(pos1,pos2-pos1);
          g_snprintf(buf, sizeof(buf), "%s(%s)", cleaned_tags[idx], tags[cleaned_tags[idx]].c_str() );
          s.push_back(quarks_p(buf, tmp));
        }
        _end2:
          ++idx;
      }

    }

    std::string build_complete (std::vector<quarks_p>& v)
    {
      std::stringstream s;
      for (int i=0;i<v.size(); ++i)
      {
        s << "\t<b>"<<v[i].first<<"</b> : "<<v[i].second<<"\n";
      }
      return s.str();
    }

    ~CertParser ()
    {
      free(issuer);
      free(subject);
    }
  };


  static void
  pretty_print_x509 (char* buf, size_t size, const Quark& server, X509* cert, bool on_connect)
  {

    if (!cert)
    {
      g_snprintf(buf,size, _("Error printing the server certificate for '%s'"), server.c_str());
      return;
    }

    struct CertParser cp(cert);
    std::vector<quarks_p> p_issuer, p_subject;
    cp.parse(p_issuer, p_subject);


    time_t t = getTimeFromASN1(cert->cert_info->validity->notAfter);
    time_t t2 = getTimeFromASN1(cert->cert_info->validity->notBefore);
    EvolutionDateMaker date_maker;
    char * until = date_maker.get_date_string (t);
    char * before = date_maker.get_date_string (t2);

    char email1[2048], email2[2048];
    char tmp1[2048], tmp2[2048];
    g_snprintf(tmp1,sizeof(tmp1), "The current server <b>'%s'</b> sent this security certificate :\n\n", server.c_str());
    g_snprintf(tmp2,sizeof(tmp2), "Certificate information for server <b>'%s'</b> :\n\n", server.c_str());

    g_snprintf(buf,size, _(     "%s"
                                "<b>Issuer information:</b>\n"
                                "%s\n"
                                "<b>Subject information: </b>\n"
                                "%s\n"
                                "<b>Valid until : </b>%s\n\n"
                                "<b>Not valid before : </b>%s\n\n"
                                "<b>Fingerprint (MD5) : </b>\n%s\n\n"),
                                on_connect ? tmp1 : tmp2,
                                cp.build_complete(p_issuer).c_str(),
                                cp.build_complete(p_subject).c_str(),
                                until,
                                before,
                                get_x509_fingerpint_md5(cert).c_str());

  }


  typedef std::multimap<std::string, Socket*> socks_m;
  typedef std::pair<std::string, Socket*> socks_p;

  static void delete_all_socks(socks_m& socket_map, std::string server)
  {

    for (socks_m::iterator it = socket_map.begin(); it != socket_map.end();)
    {
      std::cerr<<it->first<<" "<<it->second<<std::endl;
      if (it->first == server)
      {
        it->second->set_abort_flag(true);
        socket_map.erase(it++);
      } else
        ++it;
    }
  }

  static void delete_sock(socks_m& socket_map, Socket* sock)
  {
    for (socks_m::iterator it = socket_map.begin(); it != socket_map.end();)
    {
      if (it->second == sock)
      {
        delete it->second;
        socket_map.erase(it);
      }
    }
  }

}

#endif

#endif
