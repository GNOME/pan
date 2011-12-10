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

#include <config.h>
#include <vector>
#include <iostream>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include "gpg.h"

extern "C" {
  #include <stdlib.h>
  #include <unistd.h>
  #include <string.h>
}

namespace pan
{

  gpgme_ctx_t gpg_ctx;
  bool gpg_inited;
  signers_m gpg_signers;

  void deinit_gpg()
  {
    gpgme_key_t key;
    gpgme_error_t gpg_err;

    gpg_err = gpgme_op_keylist_start (gpg_ctx, 0, 0);
    while (!gpg_err)
    {
      gpg_err = gpgme_op_keylist_next (gpg_ctx, &key);
      if (!gpg_err) gpgme_key_release (key);
    }
    gpgme_release(gpg_ctx);
  }

  void fill_signer_info(GPGSignersInfo& info, gpgme_key_t key)
  {
    if (!key) return;
    if (key->uids)
    {
      if (key->uids->name) info.real_name = key->uids->name;
      if (key->uids->uid)  info.uid = key->uids->uid;
    }
    info.auth = key->can_authenticate == 1 ? true : false;
    info.sign = key->can_sign == 1 ? true : false;
    info.certify = key->can_certify == 1 ? true : false;
    info.enc = key->can_encrypt == 1 ? true : false;

    if (key->subkeys)
    {
      info.expires = key->subkeys->expires;
      info.creation_timestamp = key->subkeys->timestamp;
    }
  }

  void init_gpg()
  {
    gpgme_error_t gpg_err;

    if (gpg_inited) return;
    gpgme_check_version(0);
    gpgme_set_locale (NULL, LC_CTYPE, setlocale (LC_CTYPE, NULL));
#ifndef G_OS_WIN32
    gpgme_set_locale (NULL, LC_MESSAGES, setlocale (LC_MESSAGES, NULL));
#endif
    gpg_err = gpgme_new (&gpg_ctx);
    if (!gpg_err) gpg_inited = true; else return;

    gpgme_set_textmode (gpg_ctx, 1);
    gpgme_set_armor (gpg_ctx, 1);

    /* get all keys */
    gpgme_key_t key;

    gpg_err = gpgme_op_keylist_start (gpg_ctx, 0, 0);
    gpgme_signers_clear(gpg_ctx);

    GPGSignersInfo info;

    while (!gpg_err)
    {
      gpg_err = gpgme_op_keylist_next (gpg_ctx, &key);
      if (gpg_err) break;
      // have i forgotten anything ? ;)
      if (!key->can_certify && !key->can_sign && !key->can_authenticate && !key->can_encrypt) continue;
      if (key->revoked || key->expired || key->disabled || key->subkeys->expired) continue;
      if (key->uids->revoked || key->uids->invalid) continue;

      fill_signer_info(info, key);
      gpg_signers.insert(std::pair<std::string, GPGSignersInfo>(key->subkeys->keyid,info));
    }

    if (gpg_err_code (gpg_err) != GPG_ERR_EOF)
    {
        Log::add_err("GPG Error : can't list the keys from the keyvault, please check your settings.\n");
    }
  }

  GPGSignersInfo get_uids_from_fingerprint(char* fpr)
  {

    size_t len (strlen(fpr));
    size_t off (0);
    if (len > 16) off += len - 16;

    GPGSignersInfo empty;

    char* n = fpr+off;
    if (gpg_signers.count(n) != 0)
      return gpg_signers[n];

    return empty;
  }

}
