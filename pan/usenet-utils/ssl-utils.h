
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

/* based on verify_extract_name from tls_client.c in postfix */

/** Copyright notice: Some code taken from here :
  * http://dslinux.gits.kiev.ua/trunk/user/irssi/src/src/core/network-openssl.c
  * Copyright (C) 2002 vjt (irssi project) */

#ifndef _SSL_UTILS_H_
#define _SSL_UTILS_H_

#ifdef HAVE_OPENSSL

#include <openssl/crypto.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

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

#endif

#endif
