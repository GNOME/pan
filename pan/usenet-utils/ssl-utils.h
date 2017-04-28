
/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2011 Heinrich Müller <henmull@src.gnome.org>
 * SSL functions : Copyright (C) 2002 vjt (irssi project)
 *
 * GnuTLS functions and code
 * Copyright (C) 2002, 2003, 2004, 2005, 2007, 2008, 2010 Free Software
 * Foundation, Inc.
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

#ifndef _SSL_UTILS_H_
#define _SSL_UTILS_H_

#ifdef HAVE_GNUTLS

#include <pan/data/cert-store.h>
#include <pan/tasks/socket.h>
#include <pan/general/quark.h>
#include <pan/general/macros.h>
#include <pan/general/string-view.h>
#include <pan/tasks/socket.h>
#include <pan/general/e-util.h>
#include <gnutls/gnutls.h>
#include <gnutls/x509.h>
#include <map>
#include <sstream>
#include <iostream>
extern "C" {
  #include <glib/gi18n.h>
}

namespace pan
{

  typedef std::pair<Quark,Quark> quarks_p;
  typedef std::map<Quark,Quark>::iterator tags_it;

  const static char* tags_idx[] =
  {
    ",L=",
    ",CN=",
    ",C=",
    ",OU=",
    ",O=",
    ",ST=",
    ",EMAIL=",
    ",emailAddress=",
    ",serialNumber="
  };

  const static char* cleaned_tags[] =
  {
    "L", "CN", "C", "OU", "O", "ST", "EMAIL", "emailAdress", "serialNumber"
  };

  struct CertParser
  {
    std::map<Quark, Quark> tags;
    std::string iss, sub;
    char buf[2048];
    char * dn_buf;
    gnutls_x509_crt_t cert;
    const char delim;
    size_t len;
    gnutls_datum_t d;
    int pos1, pos2, tmp_pos1, idx;
    int num_tags;
    gnutls_x509_dn_t dn;
    size_t size;

    CertParser(gnutls_x509_crt_t c) : cert(c), delim(','), pos1(0), pos2(0), idx(0), num_tags(G_N_ELEMENTS(tags_idx))
    {

      gnutls_x509_crt_get_issuer_dn(cert, NULL, &size);
      dn_buf = new char[size];
      gnutls_x509_crt_get_issuer_dn(cert,dn_buf, &size);
      iss = dn_buf;
      delete dn_buf;

//      LEAVE this out for now
//      gnutls_x509_crt_get_subject_unique_id(cert, NULL, &size);
//      dn_buf = new char[size];
//      gnutls_x509_crt_get_subject_unique_id(cert, dn_buf, &size);
//      sub = dn_buf;
//      delete dn_buf;

      /* init map */
      int i(0);
      tags.insert(quarks_p(cleaned_tags[i++],"Locality"));
      tags.insert(quarks_p(cleaned_tags[i++],"Common Name"));
      tags.insert(quarks_p(cleaned_tags[i++],"Company"));
      tags.insert(quarks_p(cleaned_tags[i++],"Organizational Unit"));
      tags.insert(quarks_p(cleaned_tags[i++],"Organization"));
      tags.insert(quarks_p(cleaned_tags[i++],"State"));
      tags.insert(quarks_p(cleaned_tags[i++],"Email Address"));
      tags.insert(quarks_p(cleaned_tags[i],  "Email Address"));
      tags.insert(quarks_p(cleaned_tags[i],  "serialNumber"));
    }

    void parse(std::vector<quarks_p>& i, std::vector<quarks_p>& s)
    {
      while(idx < num_tags)
      {
        std::string::size_type index = iss.find(tags_idx[idx]);
        if(index != std::string::npos)
        {
          pos1 = (int)index + strlen(tags_idx[idx]);
          pos2 = (int)iss.find(delim, pos1);
          if (pos2<=0) goto _end;
          // seperate handling for CN tag
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
      for (size_t i=0; i< v.size(); ++i)
      {
        s << "\t<b>"<<v[i].first<<"</b> : "<<v[i].second<<"\n";
      }
      return s.str();
    }

    ~CertParser ()
    {
    }
  };

  static void
  pretty_print_x509 (char* buf, size_t size, const Quark& server, gnutls_x509_crt_t c, bool on_connect)
  {

    if (!c)
    {
      g_snprintf(buf,size, _("Error printing the server certificate for “%s”"), server.c_str());
      return;
    }

    CertParser cp(c);
    std::vector<quarks_p> p_issuer, p_subject;
    cp.parse(p_issuer, p_subject);


    time_t t  = gnutls_x509_crt_get_expiration_time(c);
    time_t t2 = gnutls_x509_crt_get_activation_time(c);
    EvolutionDateMaker date_maker;
    char * until = date_maker.get_date_string (t);
    char * before = date_maker.get_date_string (t2);

    char tmp1[2048], tmp2[2048];
    g_snprintf(tmp1,sizeof(tmp1), _("The current server <b>“%s”</b> sent this security certificate:\n\n"), server.c_str());
    g_snprintf(tmp2,sizeof(tmp2), _("Certificate information for server <b>“%s”</b>:\n\n"), server.c_str());

    g_snprintf(buf,size, _("%s"
                           "<b>Issuer information:</b>\n"
                           "%s\n"
                           "<b>Valid until:</b> %s\n\n"
                           "<b>Not valid before:</b> %s\n\n"),
                           on_connect ? tmp1 : tmp2,
                           cp.build_complete(p_issuer).c_str(),
                           until,
                           before);

    g_free (before);
    g_free (until);

  }


}

#endif

#endif
