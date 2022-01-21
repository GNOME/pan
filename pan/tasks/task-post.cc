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
#include <glib/gi18n.h>
#include "task-post.h"
#include <pan/general/debug.h>
#include <pan/general/log.h>

using namespace pan;

namespace
{
  std::string get_description (GMimeMessage * message)
  {
    char buf[1024];
    snprintf (buf, sizeof(buf), _("Posting \"%s\""), g_mime_message_get_subject(message));
    return buf;
  }
}

TaskPost :: TaskPost (const Quark& server, GMimeMessage * message):
  Task ("POST", get_description(message)),
  _server (server),
  _message (message)
{
  g_object_ref (G_OBJECT(_message));

  _state.set_need_nntp (server);
}

TaskPost :: ~TaskPost ()
{
  g_object_unref (G_OBJECT(_message));
}

void
TaskPost :: use_nntp (NNTP * nntp)
{
  _state.set_working ();

  char * text = g_mime_object_to_string (GMIME_OBJECT(_message), NULL);
  nntp->post (text, this);
  g_free (text);
}

void
TaskPost :: on_nntp_done (NNTP              * nntp,
                          Health              health,
                          const StringView  & response)
{
  _state.set_health (health);

  const char* res(g_mime_message_get_subject(_message));

  if (health == ERR_NETWORK || health == ERR_COMMAND)
  {
    Log :: add_err_va (_("Posting of \"%s\" failed: %s"),
                          res, response.str);
    _state.set_need_nntp (_server);
  }
  else {
    Log :: add_info_va (_("Posting of \"%s\" successful: %s"),
                          res, response.str);
    _state.set_completed ();
    set_error (response);
    set_finished (health);
  }

  check_in (nntp, health);
}
