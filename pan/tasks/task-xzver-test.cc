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
#include <cassert>
#include <cerrno>
extern "C" {
  #include <glib/gi18n.h>
  #include <gmime/gmime-utils.h>
}
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/messages.h>
#include <pan/general/utf8-utils.h>
#include <pan/data/data.h>
#include "nntp.h"
#include "task-xzver-test.h"

using namespace pan;

namespace
{
  std::string get_description (void)
  {
    char buf[1024];
    snprintf (buf, sizeof(buf), _("Testing for XZVER Header compression on current server ...."));
    return std::string (buf);
  }
}

TaskXZVerTest :: TaskXZVerTest (Data         & data,
                        const Quark  & server) :
  Task("XOVER", get_description()),
  _data (data),
  _server (server)
{
  _state.set_need_nntp(_server);
}

TaskXZVerTest :: ~TaskXZVerTest ()
{}

void
TaskXZVerTest :: use_nntp (NNTP* nntp)
{
  nntp->group (Quark("alt.binaries.test"), this);
}

/***
****
***/

void
TaskXZVerTest :: on_nntp_group (NNTP          * nntp,
                            const Quark   & group,
                            unsigned long   qty,
                            uint64_t        low,
                            uint64_t        high)
{
  nntp->xzver(group, high-100, high,this);
}

void
TaskXZVerTest :: on_nntp_line (NNTP               * nntp,
                           const StringView   & line)
{}

void
TaskXZVerTest :: on_what   (NNTP               * nntp,
                            const StringView   & line)
{
  _state.set_completed ();
  set_finished (OK);
  check_in (nntp, OK);
  _data.set_server_xzver_support(nntp->_server,0);
}

void
TaskXZVerTest :: on_xover_follows  (NNTP               * nntp,
                                    const StringView   & line)
{
  _state.set_completed ();
  set_finished (OK);
  check_in (nntp, OK);
  _data.set_server_xzver_support(nntp->_server,1);
}


void
TaskXZVerTest :: on_nntp_done (NNTP              * nntp,
                               Health              health,
                               const StringView  & response UNUSED)
{
  _state.set_completed ();
  set_finished (OK);
  check_in (nntp, health);
}

