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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "ssl-utils.h"

#ifdef HAVE_GNUTLS

#include <pan/general/e-util.h>
#include <pan/general/quark.h>

#include <glib/gi18n.h>

#include <stddef.h>
#include <string.h>

#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace pan {
namespace {

typedef std::pair<Quark, Quark> quarks_p;
typedef std::map<Quark, Quark>::iterator tags_it;

const static char *tags_idx[] = {",L=",
                                 ",CN=",
                                 ",C=",
                                 ",OU=",
                                 ",O=",
                                 ",ST=",
                                 ",EMAIL=",
                                 ",emailAddress=",
                                 ",serialNumber="};

const static char *cleaned_tags[] = {
  "L", "CN", "C", "OU", "O", "ST", "EMAIL", "emailAdress", "serialNumber"};

static char const *const tags[] = {
  "Locality",
  "Common Name",
  "Company",
  "Organizational Unit",
  "Organization",
  "State",
  "Email Address",
  "Email Address", //Yes. there are TWO names for email.
  "serialNumber" // Really???
};

class CertParser
{
  public:
    explicit CertParser(gnutls_x509_crt_t cert) :
      delim_(','),
      num_tags_(G_N_ELEMENTS(tags_idx))
    {

      {
        size_t size;
        gnutls_x509_crt_get_issuer_dn(cert, NULL, &size);
        char *dn_buf = new char[size];
        gnutls_x509_crt_get_issuer_dn(cert, dn_buf, &size);
        iss_ = dn_buf;
        delete[] dn_buf;
      }

      for (std::size_t i = 0; i < num_tags_; i += 1)
      {
        tags_.insert(quarks_p(cleaned_tags[i], tags[i]));
      }
    }

    CertParser(CertParser const &) = delete;
    CertParser &operator=(CertParser const &) = delete;

    ~CertParser()
    {
    }

    void parse(std::vector<quarks_p> &issuer)
    {
      char buf[2048];
      for (int idx = 0; idx < num_tags_; idx += 1)
      {
        std::string::size_type index = iss_.find(tags_idx[idx]);
        if (index == std::string::npos)
        {
          continue;
        }
        std::string::size_type pos1 = index + strlen(tags_idx[idx]);
        std::string::size_type pos2 = iss_.find(delim_, pos1);
        if (pos2 == std::string::npos)
        {
          continue;
        }
        // seperate handling for CN tag
        if (strcmp(cleaned_tags[idx], "CN") == 0)
        {
          std::string::size_type tmp_pos = (int)iss_.find("://", pos2 - 1);
          if (tmp_pos == pos2 - 1)
          {
            pos2 = iss_.find(delim_, pos2 + 2);
          }
        }
        std::string tmp = iss_.substr(pos1, pos2 - pos1);
        g_snprintf(buf,
                   sizeof(buf),
                   "%s (%s)",
                   cleaned_tags[idx],
                   tags_[cleaned_tags[idx]].c_str());
        issuer.push_back(quarks_p(buf, tmp));
      }
    }

    std::string build_complete(std::vector<quarks_p> const &v)
    {
      std::stringstream s;
      for (size_t i = 0; i < v.size(); ++i)
      {
        s << "\t<b>" << v[i].first << "</b> : " << v[i].second << "\n";
      }
      return s.str();
    }

  private:
    std::map<Quark, Quark> tags_;
    std::string iss_;
    char const delim_;
    int const num_tags_;
};

} // namespace

void pretty_print_x509(char *buf,
                       size_t size,
                       Quark const &server,
                       gnutls_x509_crt_t c,
                       bool on_connect)
{

  if (c == nullptr)
  {
    g_snprintf(buf,
               size,
               _("Error printing the server certificate for '%s'"),
               server.c_str());
    return;
  }

  CertParser cp(c);
  std::vector<quarks_p> p_issuer;
  cp.parse(p_issuer);

  time_t t = gnutls_x509_crt_get_expiration_time(c);
  time_t t2 = gnutls_x509_crt_get_activation_time(c);
  EvolutionDateMaker date_maker;
  char *until = date_maker.get_date_string(t);
  char *before = date_maker.get_date_string(t2);

  char tmp1[2048];
  g_snprintf(
    tmp1,
    sizeof(tmp1),
    _("The current server <b>'%s'</b> sent this security certificate:\n\n"),
    server.c_str());

  char tmp2[2048];
  g_snprintf(tmp2,
             sizeof(tmp2),
             _("Certificate information for server <b>'%s'</b>:\n\n"),
             server.c_str());

  g_snprintf(buf,
             size,
             _("%s"
               "<b>Issuer information:</b>\n"
               "%s\n"
               "<b>Valid until:</b> %s\n\n"
               "<b>Not valid before:</b> %s\n\n"),
             on_connect ? tmp1 : tmp2,
             cp.build_complete(p_issuer).c_str(),
             until,
             before);

  g_free(before);
  g_free(until);
}

} // namespace pan

#endif
