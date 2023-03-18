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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
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

#include <glib/gi18n.h>
#include <gmime/gmime.h>

#ifdef HAVE_GMIME_CRYPTO

namespace pan
{

  static gboolean
  request_passwd (GMimeCryptoContext *ctx, const char *user_id, const char *prompt_ctx,
                  gboolean reprompt, GMimeStream *response, GError **err)
  {
    return TRUE;
  }

  GMimeCryptoContext *gpg_ctx;
  bool gpg_inited;


  void deinit_gpg()
  {
    gpg_inited=false;
    g_object_unref (gpg_ctx);
  }

  void fill_signer_info(GPGSignersInfo& info, GMimeSignatureList * sig_list)
  {
    g_return_if_fail(sig_list);

    int l = g_mime_signature_list_length(sig_list);
    info.signers.reserve(l);
    GMimeSignature * sig(nullptr);
    Signer signer;
    for (int i=0;i<l;++i)
    {
      sig = g_mime_signature_list_get_signature (sig_list, i);

      signer.name = sig->cert->name ? sig->cert->name : "(null)";
      signer.key_id = sig->cert->keyid ? sig->cert->keyid : "(null)";
      signer.fpr = sig->cert->fingerprint ? sig->cert->fingerprint : "(null)";

      switch (sig->cert->trust) {
      case GMIME_TRUST_UNKNOWN:
        signer.trust = "None";
        break;
      case GMIME_TRUST_NEVER:
        signer.trust = "Never";
        break;
      case GMIME_TRUST_UNDEFINED:
        signer.trust = "Undefined";
        break;
      case GMIME_TRUST_MARGINAL:
        signer.trust = "Marginal";
        break;
      case GMIME_TRUST_FULL:
        signer.trust = "Fully";
        break;
      case GMIME_TRUST_ULTIMATE:
        signer.trust = "Ultimate";
        break;
      }

      switch (sig->status) {
      case GMIME_SIGNATURE_STATUS_GREEN:
        signer.status = "GOOD";
        break;
      case GMIME_SIGNATURE_STATUS_RED:
        signer.status = "BAD";
        break;
      case GMIME_SIGNATURE_STATUS_SYS_ERROR:
        signer.status = "ERROR";
        break;
      }

      signer.created = sig->created;
      signer.expires = sig->expires;
      if (sig->expires == (time_t) 0)
        signer.never_expires = true;

      signer.created = sig->created;
      signer.expires = sig->expires;
      if (sig->expires == (time_t) 0)
        signer.never_expires = true;

// https://developer-old.gnome.org/gmime/stable/gmime-changes-3-0.html
// GMimeSignatureStatus and GMimeSignatureErrors have been merged into a single bitfield (GMimeSignatureStatus) ...
//      if (sig->errors) {

//        if (sig->errors & GMIME_SIGNATURE_ERROR_EXPSIG)
//          signer.error = "Expired";
//        if (sig->errors & GMIME_SIGNATURE_ERROR_NO_PUBKEY)
//          signer.error = "No Pub Key";
//        if (sig->errors & GMIME_SIGNATURE_ERROR_EXPKEYSIG)
//          signer.error = "Key Expired";
//        if (sig->errors & GMIME_SIGNATURE_ERROR_REVKEYSIG)
//          signer.error = "Key Revoked";
//      } else {
//          signer.error = "No errors for this signer";
//      }

      info.signers.push_back(signer);
    }
  }

  void init_gpg()
  {
    gpg_ctx = g_mime_gpg_context_new();
    if (!gpg_ctx) gpg_inited = false; else gpg_inited = true;
//    g_mime_gpg_context_set_auto_key_retrieve(GMIME_GPG_CONTEXT(gpg_ctx),true);
//    g_mime_gpg_context_set_always_trust(GMIME_GPG_CONTEXT(gpg_ctx),false);
//    g_mime_gpg_context_set_use_agent(GMIME_GPG_CONTEXT(gpg_ctx), false);
  }


}
#endif // g_mime_crypto
