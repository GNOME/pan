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

#ifndef _SSL_UTILS_H_
#define _SSL_UTILS_H_

#include "config.h"

#ifdef HAVE_GNUTLS

#include <gnutls/x509.h>

#include <stddef.h>

namespace pan {

class Quark;

void pretty_print_x509(char *buf,
                       size_t size,
                       Quark const &server,
                       gnutls_x509_crt_t c,
                       bool on_connect);
} // namespace pan

#endif

#endif
