/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This file
 * Copyright (C) 2011 Heinrich Müller <henmull@src.gnome.org>
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

#include <string>

extern "C" {
  #include <sys/stat.h>
}

#include <pan/tasks/socket.h>
#include <config.h>
#include <map>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <string>

#include <glib/gi18n.h>
#include <glib.h>

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

namespace pan {

struct SaveCBStruct {
	CertStore& cs;
	const Quark server;
	Data& data;
	SaveCBStruct(CertStore& store, const Quark& s, Data& d) :
			cs(store), server(s), data(d) {
	}
};

gboolean save_server_props_cb(gpointer gp) {
	SaveCBStruct* data(static_cast<SaveCBStruct*>(gp));
	data->data.save_server_info(data->server);
	delete data;
	return false;
}

int verify_callback(gnutls_session_t session) {

	mydata_t* mydata = (mydata_t*) gnutls_session_get_ptr(session);

	unsigned int status;
	const gnutls_datum_t *cert_list;
	unsigned int cert_list_size;
	int ret;
	gnutls_x509_crt_t cert;
	bool fail(false);
	bool fatal(false);

	ret = gnutls_certificate_verify_peers2(session, &status);

	if (ret < 0)
		return GNUTLS_E_CERTIFICATE_ERROR;

	if (status & GNUTLS_CERT_INVALID) {
		if (!mydata->always_trust) {
			g_warning("The certificate is not trusted.\n");
			fail = true;
		}
	}

	if (status & GNUTLS_CERT_SIGNER_NOT_FOUND) {
		if (!mydata->always_trust) {
			g_warning("The certificate does not have a known issuer.\n");
			fail = true;
		}
	}

	if (status & GNUTLS_CERT_REVOKED) {
		if (!mydata->always_trust) {
			g_warning("The certificate has been revoked.\n");
			fail = true;
		}
	}

	if (status & GNUTLS_CERT_EXPIRED) {
		if (!mydata->always_trust) {
			g_warning("The certificate has expired\n");
			fail = true;
		}
	}

	if (status & GNUTLS_CERT_NOT_ACTIVATED) {
		if (!mydata->always_trust) {
			g_warning("The certificate is not yet activated\n");
			fail = true;
		}
	}

	/* Up to here the process is the same for X.509 certificates and
	 * OpenPGP keys. From now on X.509 certificates are assumed. This can
	 * be easily extended to work with openpgp keys as well.
	 */
	if (gnutls_certificate_type_get(session) != GNUTLS_CRT_X509) {
		g_warning("The certificate is not a X509 certificate!\n");
		fail = true;
		fatal = true;
	}

	if (gnutls_x509_crt_init(&cert) < 0) {
		g_warning("Error in initialization\n");
		fail = true;
		goto _fatal;
	}

	cert_list = gnutls_certificate_get_peers(session, &cert_list_size);
	if (cert_list == NULL) {
		g_warning("No certificate found!\n");
		fail = true;
		goto _fatal;
	}

	/* TODO verify whole chain perhaps?
	 */
	if (gnutls_x509_crt_import(cert, &cert_list[0], GNUTLS_X509_FMT_DER) < 0) {
		g_warning("Error parsing certificate!\n");
		fail = true;
		goto _fatal;
	}

	if (!gnutls_x509_crt_check_hostname(cert, mydata->hostname_full.c_str())) {
		if (!mydata->always_trust) {
			g_warning(
					"The certificate's owner does not match hostname '%s' !\n", mydata->hostname_full.c_str());
			fail = true;
		}
	}

	/* auto-add new cert if we always trust this server and the cert isn't already stored in the store */
	/* fail is only set if we don't always trust this server and a critical condition occurred, e.g. hostname mismatch */
	if (mydata->always_trust && ret < 0)
		mydata->cs->add(cert, mydata->host);
	else if (fail)
		goto _fail;

	/* notify gnutls to continue handshake normally */
	return 0;

	_fatal: gnutls_x509_crt_deinit(cert);
	return GNUTLS_E_CERTIFICATE_ERROR;

	_fail: mydata->cs->verify_failed(cert, mydata->host.c_str(), status);
	return GNUTLS_E_CERTIFICATE_ERROR;

}

bool CertStore::import_from_file(const Quark& server, const char* fn) {

	size_t filelen;

	Data::Server* s(_data.find_server(server));
	if (!s)
		return false;
	if (s->cert.empty())
		return false;

	const char* filename(
			fn ? fn : file::absolute_fn("ssl_certs", s->cert).c_str());
	if (!filename)
		return false;

	FILE * fp = fopen(filename, "rb");
	if (!fp)
		return false;

	fseek(fp, 0, SEEK_END);
	filelen = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	char * buf = new char[filelen];
	size_t dummy(fread(buf, sizeof(char), filelen, fp)); // silence compiler
	fclose(fp);

	gnutls_datum_t in;
	in.data = (unsigned char*) buf;
	in.size = filelen;
	gnutls_x509_crt_t cert;
	gnutls_x509_crt_init(&cert);
	gnutls_x509_crt_import(cert, &in, GNUTLS_X509_FMT_PEM);

	delete[] buf;

	int ret = gnutls_certificate_set_x509_trust(_creds, &cert, 1);

	if (ret < 0) {
		s->cert.clear();
		gnutls_x509_crt_deinit(cert);
		return false;
	}

	_cert_to_server[server] = cert;

	return true;
}

int CertStore::get_all_certs_from_disk() {

	int cnt(0);
	quarks_t servers(_data.get_servers());
	int ret(0);
	GError* err(NULL);

	foreach_const(quarks_t, servers, it){
	if (import_from_file(*it))
	{
		++cnt;
	}
	else
	{
		Data::Server* s(_data.find_server(*it));
		s->cert.clear();
	}
}

// get certs from ssl certs directory
	char * ssldir(0);
	ssldir = getenv("SSL_CERT_DIR");
	if (!ssldir)
		ssldir = getenv("SSL_DIR");
	if (!ssldir)
		return cnt;

	GDir * dir = g_dir_open(ssldir, 0, &err);
	if (err != NULL) {
		Log::add_err_va(
				_("Error opening SSL certificate directory: \"%s\": %s"),
				ssldir, err->message);
		g_error_free(err);
	} else {
		char filename[PATH_MAX];
		const char * fname;
		while ((fname = g_dir_read_name(dir))) {
			struct stat stat_p;
			g_snprintf(filename, sizeof(filename), "%s%c%s", ssldir,
					G_DIR_SEPARATOR, fname);
			if (!stat(filename, &stat_p)) {
				if (!S_ISREG(stat_p.st_mode))
					continue;
				ret = gnutls_certificate_set_x509_trust_file(_creds, filename,
						GNUTLS_X509_FMT_PEM);
				if (ret > 0)
					cnt += ret;
			}
		}
		g_dir_close(dir);
	}

	return cnt;
}

void CertStore::init() {
	int r(0);
	r = get_all_certs_from_disk();

	if (r != 0)
		Log::add_info_va(
				_("Successfully added %d SSL PEM certificate(s) to Certificate Store."),
				r);

}

void CertStore::remove_hard(const Quark& server) {
	std::string fn = _data.get_server_cert(server);
	unlink(fn.c_str());
}

void CertStore::remove(const Quark& server) {
	_cert_to_server.erase(server);
	remove_hard(server);
}

CertStore::CertStore(Data& data) :
		_data(data) {
	_path = file::absolute_fn("ssl_certs", "");
	if (!file::ensure_dir_exists(_path)) {
		std::cerr
				<<_("Error initializing Certificate Store. Check that the permissions for the folders "
						"~/.pan2 and ~/.pan2/ssl_certs are set correctly. Fatal, exiting.");
		file::print_file_info(std::cerr, _path.c_str());
		exit(EXIT_FAILURE);
	}

	gnutls_certificate_allocate_credentials(&_creds);
	gnutls_certificate_set_verify_function(_creds, verify_callback);

}

CertStore::~CertStore() {
	gnutls_certificate_free_credentials(_creds);
	foreach (certs_m, _cert_to_server, it)if (it->second)
	gnutls_x509_crt_deinit(it->second);
}

bool CertStore::add(gnutls_x509_crt_t cert, const Quark& server) {
	if (!cert || server.empty())
		return false;

	std::string addr, cert_file_name, cert_file_name_wp;
	int port;
	_data.get_server_addr(server, addr, port);
	_cert_to_server[server] = cert;

	std::stringstream buffer;
	buffer << addr << ".pem";
	cert_file_name = buffer.str();
	cert_file_name_wp = file::absolute_fn("ssl_certs", cert_file_name);

	FILE * fp = fopen(cert_file_name_wp.c_str(), "wb");
	if (!fp)
		return false;

	_data.set_server_cert(server, cert_file_name.c_str());

	SaveCBStruct* cbstruct = new SaveCBStruct(*this, server, _data);
	g_idle_add(save_server_props_cb, cbstruct);

	int rc1 = 99;
	int rc2 = 99;
	size_t outsize = 0;
	/* make up for dumbness of this function */
	rc1 = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, NULL, &outsize);
	char* out = new char[outsize];
	rc2 = gnutls_x509_crt_export(cert, GNUTLS_X509_FMT_PEM, out, &outsize);

	if (rc2 != 0) {
	Log::add_err_va (_("Could not export certificate for server: %s"), addr.c_str());
	} else {
	fputs((const char*) out, fp);
	}

	debug_SSL_verbatim("\n===========================================");
	debug_SSL_verbatim(out);
	debug_SSL_verbatim("\n===========================================");

	delete[] out;
	fclose(fp);
	chmod(cert_file_name_wp.c_str(), 0600);

	gnutls_certificate_set_x509_trust(_creds, &cert, 1); // for now, only 1 is saved
	valid_cert_added(cert, server.c_str());

	debug("adding server cert "<<server<<" "<<cert);

	return true;
}

}  // namespace pan

#endif
