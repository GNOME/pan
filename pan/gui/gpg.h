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

#ifndef _HAVE_GPGDEFS_H
#define _HAVE_GPGDEFS_H

#include <gpgme.h>
#include <map>

namespace pan
{

  struct GPGSignersInfo
  {
    std::string real_name; // real world name
    bool auth, sign, certify, enc; // used to ....
    long int expires; // expired ?
    std::string uid;  // userid /hex
    long int creation_timestamp;

    GPGSignersInfo() : real_name(""), auth(false), sign(false),
        certify(false), enc(false), expires(0), uid(""), creation_timestamp(0) {}
  };

  typedef std::map<std::string,GPGSignersInfo> signers_m;

  extern gpgme_ctx_t gpg_ctx;
  extern bool gpg_inited;
  extern signers_m gpg_signers;

  /* Error struct for gpg_decrypt */
  struct GPGDecErr
  {
    gpg_error_t err;
    gpgme_decrypt_result_t dec_res;
    gpgme_verify_result_t v_res;
    bool dec_ok;
    bool verify_ok;
    bool no_sigs;

    GPGDecErr() : dec_ok(false), verify_ok(false), no_sigs(true), err(GPG_ERR_NO_ERROR) {}
  };

  /* Error struct for gpg_sign_and_encrypt */
  struct GPGEncErr
  {
    gpgme_error_t err;
    gpgme_encrypt_result_t enc_res;
    gpgme_sign_result_t sign_res;

    GPGEncErr() : err(GPG_ERR_NO_ERROR) {}

  };

  GPGSignersInfo get_uids_from_fingerprint(char*);
  void init_gpg();
  void deinit_gpg();
  void fill_signer_info(GPGSignersInfo& info, gpgme_key_t key);

}

#endif

