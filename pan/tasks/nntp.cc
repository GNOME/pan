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
#include <cstdarg>
#include <cstdlib> // abort, atoi, strtoul
#include <cstdio> // snprintf
extern "C" {
  #include <glib.h>
  #include <glib/gi18n.h>
}
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/messages.h>
#include "nntp.h"

using namespace pan;

namespace
{
   std::string
   build_command (const char * fmt, ...)
   {
      std::string cmd;

      if (fmt)
      {
         va_list args;
         va_start (args, fmt);
         char * str = g_strdup_vprintf (fmt, args);
         va_end (args);
         cmd = str;
         g_free (str);
      }

      return cmd;
   }
};

namespace
{
   enum
   {
      AUTH_REQUIRED              = 480,
      AUTH_NEED_MORE             = 381,
      AUTH_ACCEPTED              = 281,
      AUTH_REJECTED              = 482,

      SERVER_READY               = 200,
      SERVER_READY_NO_POSTING    = 201,
      SERVER_READY_STREAMING_OK  = 203,

      GOODBYE                    = 205,

      GROUP_RESPONSE             = 211,
      GROUP_NONEXISTENT          = 411,

      INFORMATION_FOLLOWS        = 215,

      XOVER_FOLLOWS              = 224,
      XOVER_NO_ARTICLES          = 420,

      ARTICLE_FOLLOWS            = 220,

      NEWGROUPS_FOLLOWS          = 231,

      ARTICLE_POSTED_OK          = 240,
      SEND_ARTICLE_NOW           = 340,
      NO_POSTING                 = 440,
      POSTING_FAILED             = 441,

      TOO_MANY_CONNECTIONS       = 400,

      NO_GROUP_SELECTED          = 412,
      NO_SUCH_ARTICLE_NUMBER     = 423,
      NO_SUCH_ARTICLE            = 430,

      ERROR_CMD_NOT_UNDERSTOOD   = 500,
      ERROR_CMD_NOT_SUPPORTED    = 501,
      NO_PERMISSION              = 502,
      FEATURE_NOT_SUPPORTED      = 503
   };
}

void
NNTP :: fire_done_func (Health health, const StringView& response)
{
   if (_listener)
   {
      Listener * l = _listener;
      debug ("I (" << (void*)this << ") am setting my _listener to 0");
      _listener = 0;
      l->on_nntp_done (this, health, response);
   }
}

/***
****  WRITING
***/

bool
NNTP :: on_socket_response (Socket * sock UNUSED, const StringView& line_in)
{
   enum State { CMD_FAIL, CMD_DONE, CMD_MORE, CMD_NEXT, CMD_RETRY };
   State state;
   StringView line (line_in);

   // strip off trailing \r\n
   if (line.len>=2 && line.str[line.len-2]=='\r' && line.str[line.len-1]=='\n')
      line.truncate (line.len-2);

//debug ("_nntp_response_text: " << _nntp_response_text);
   if (_nntp_response_text)
   {
      if (line.len==1 && line.str[0]=='.') // end-of-list
      {
         state = CMD_DONE;
         _nntp_response_text = false;
      }
      else
      {
         state = CMD_MORE;

         if (line.len>=2 && line.str[0]=='.' && line.str[1]=='.') // rfc 977: 2.4.1
            line.rtruncate (line.len-1);

         assert (_listener != 0);
         if (_listener)
            _listener->on_nntp_line (this, line);
      }
   }
   else switch (atoi (line.str))
   {
      case SERVER_READY:
      case SERVER_READY_NO_POSTING:
      case SERVER_READY_STREAMING_OK:
         state = CMD_DONE;
         break;

      case ARTICLE_POSTED_OK:
      case GOODBYE:
      case XOVER_NO_ARTICLES:
         state = CMD_DONE;
         break;

      case AUTH_REQUIRED: { // must send username
         if (!_username.empty()) {
           _commands.push_front (_previous_command);
           _socket->write_command_va (this, "AUTHINFO USER %s\r\n", _username.c_str());
           state = CMD_NEXT;
         } else {
           std::string host;
           _socket->get_host (host);
           Log::add_err_va (_("%s requires a username, but none is set."), host.c_str());
           state = CMD_FAIL;
         }
         break;
      }

      case AUTH_NEED_MORE: { // must send password
        if (!_password.empty()) {
           _socket->write_command_va (this, "AUTHINFO PASS %s\r\n", _password.c_str());
           state = CMD_NEXT;
        } else {
           std::string host;
           _socket->get_host (host);
           Log::add_err_va (_("%s requires a password, but none is set."), host.c_str());
           state = CMD_FAIL;
        }
        break;
      }

      case AUTH_ACCEPTED:
         state = CMD_DONE;
         break;

      case GROUP_RESPONSE: {
         // response is of form "211 qty low high group_name"
         StringView tok, myline (line);
         myline.pop_token (tok, ' ');
         myline.pop_token (tok, ' ');
         const unsigned long aqty (strtoul (tok.str, NULL, 10));
         myline.pop_token (tok, ' ');
         const unsigned long alo (strtoul (tok.str, NULL, 10));
         myline.pop_token (tok, ' ');
         const unsigned long ahi (strtoul (tok.str, NULL, 10));
         myline.pop_token (tok, ' ');
         const pan::Quark group (tok);
         if (_listener)
            _listener->on_nntp_group (this, group, aqty, alo, ahi);
         _group = group;
          state = CMD_DONE;
         break;
      }

      case SEND_ARTICLE_NOW:
         // ready to get article; send it now
         _socket->write_command (_post, this);
         state = CMD_NEXT;
         break;

      case NO_POSTING:
      case POSTING_FAILED:
      case GROUP_NONEXISTENT:
         state = CMD_FAIL;
         break;

      case XOVER_FOLLOWS:
      case ARTICLE_FOLLOWS:
      case NEWGROUPS_FOLLOWS:
      case INFORMATION_FOLLOWS:
         state = CMD_MORE;
         _nntp_response_text = true;
         break;

      case AUTH_REJECTED:
      case NO_GROUP_SELECTED:
      case ERROR_CMD_NOT_UNDERSTOOD:
      case ERROR_CMD_NOT_SUPPORTED:
      case NO_PERMISSION:
      case FEATURE_NOT_SUPPORTED: {
         std::string cmd (_previous_command);
         if (cmd.size()>=2 && cmd[cmd.size()-1]=='\n' && cmd[cmd.size()-2]=='\r')
           cmd.resize (cmd.size()-2);
         std::string host;
         _socket->get_host (host);
         Log::add_err_va (_("Sending \"%s\" to %s returned an error: %s"),
                          cmd.c_str(),
                          host.c_str(),
                          line.to_string().c_str());
         state = CMD_FAIL;
         break;
      }

      case NO_SUCH_ARTICLE_NUMBER:
      case NO_SUCH_ARTICLE:
         state = CMD_FAIL;
         break;

      case TOO_MANY_CONNECTIONS:
         state = CMD_RETRY;
         break;

      default: {
         std::string cmd (_previous_command);
         if (cmd.size()>=2 && cmd[cmd.size()-1]=='\n' && cmd[cmd.size()-2]=='\r')
           cmd.resize (cmd.size()-2);
         std::string host;
         _socket->get_host (host);
         Log::add_err_va (_("Sending \"%s\" to %s returned an unrecognized response: \"%s\""),
                          _previous_command.c_str(),
                          host.c_str(),
                          line.to_string().c_str());
         state = CMD_FAIL;
         break;
      }
   }

   if ((state == CMD_DONE) && !_commands.empty())
   {
     write_next_command ();
     state = CMD_NEXT;
   }

   bool more;
   switch (state) {
      case CMD_FAIL: fire_done_func (ERR_COMMAND, line); more = false; break;
      case CMD_DONE: if (_commands.empty()) fire_done_func (OK, line); more = false; break;
      case CMD_MORE: more = true; break; // keep listining for more on this command
      case CMD_NEXT: more = false; break; // no more responses on this command; wait for next...
      case CMD_RETRY: fire_done_func (ERR_NETWORK, line); more = false; break;
      default: abort(); break;
   }
   return more;
}

void
NNTP :: on_socket_abort (Socket * sock UNUSED)
{
   fire_done_func (ERR_NETWORK, StringView());
}

void
NNTP :: on_socket_error (Socket * sock UNUSED)
{
   _socket_error = true;
   fire_done_func (ERR_NETWORK, StringView());
}

namespace
{
   void
   ensure_trailing_crlf (GString * g)
   {
      if (g->len<2 || g->str[g->len-2]!='\r' || g->str[g->len-1]!='\n')
         g_string_append (g, "\r\n");
   }
};

void
NNTP :: write_next_command ()
{
   assert (!_commands.empty());

   //for (strings_t::const_iterator it=_commands.begin(), end=_commands.end(); it!=end; ++it)
   //   debug ("command [" << *it << ']');

   _previous_command = _commands.front ();
   _commands.pop_front ();
   debug ("nntp " << this << " writing to socket " << _socket << " on server " << _server << " this command: [" << _previous_command << ']');
   _socket->write_command (_previous_command, this);
}

/***
****
***/

void
NNTP :: xzver (const Quark   & group,
               uint64_t        low,
               uint64_t        high,
               Listener      * l)
{
   _listener = l;

   if (group != _group)
      _commands.push_back (build_command ("GROUP %s\r\n", group.c_str()));

   _commands.push_back (build_command ("XZVER %"G_GUINT64_FORMAT"-%"G_GUINT64_FORMAT"\r\n", low, high));

   write_next_command ();
}


void
NNTP :: xover (const Quark   & group,
               uint64_t        low,
               uint64_t        high,
               Listener      * l)
{
   _listener = l;

   if (group != _group)
      _commands.push_back (build_command ("GROUP %s\r\n", group.c_str()));

   _commands.push_back (build_command ("XOVER %"G_GUINT64_FORMAT"-%"G_GUINT64_FORMAT"\r\n", low, high));

   write_next_command ();
}

void
NNTP :: list_newsgroups (Listener * l)
{
   _listener = l;
   _commands.push_back ("LIST NEWSGROUPS\r\n");
   write_next_command ();
}

void
NNTP :: list (Listener * l)
{
   _listener = l;
   _commands.push_back ("LIST\r\n");
   write_next_command ();
}

void
NNTP :: article (const Quark     & group,
                 uint64_t          article_number,
                 Listener        * l)
{
   _listener = l;

   if (group != _group)
      _commands.push_back (build_command ("GROUP %s\r\n", group.c_str()));

   _commands.push_back (build_command ("ARTICLE %"G_GUINT64_FORMAT"\r\n", article_number));

   write_next_command ();
}

void
NNTP :: article (const Quark     & group,
                 const char      * message_id,
                 Listener        * l)
{
   _listener = l;

   if (group != _group)
      _commands.push_back (build_command ("GROUP %s\r\n", group.c_str()));

   _commands.push_back (build_command ("ARTICLE %s\r\n", message_id));

   write_next_command ();
}

void
NNTP :: group (const Quark  & group,
               Listener     * l)
{
   _listener = l;

   _commands.push_back (build_command ("GROUP %s\r\n", group.c_str()));
   debug ("_commands.size(): " << _commands.size());
   write_next_command ();
}


void
NNTP :: goodbye (Listener * l)
{
   _listener = l;
   _commands.push_back ("QUIT\r\n");
   write_next_command ();
}

void
NNTP :: handshake (Listener * l)
{
  _listener = l;

  // queue up two or three commands:
  // (1) handshake, which is an empty string
  // (2) if we've got a username, offer it to the server.
  // (3) mode reader.  the `group' command is only available after `mode reader'. (#343814)
  _commands.push_back ("");
  if (!_username.empty()) {
    char buf[512];
    snprintf (buf, sizeof(buf), "AUTHINFO USER %s\r\n", _username.c_str());
    _commands.push_back (buf);
  }
  _commands.push_back ("MODE READER\r\n");

  write_next_command ();
}

void
NNTP :: noop (Listener * l)
{
   _listener = l;
   _commands.push_back ("MODE READER\r\n");
   write_next_command ();
}

namespace
{
  // non-recursive search and replace.
  void replace_linear (std::string& s, const char* old_text, const char * new_text)
  {
    std::string::size_type pos (0);
    while (((pos = s.find (old_text, pos))) != std::string::npos) {
      s.replace (pos, strlen(old_text), new_text);
      pos += strlen(new_text);
    }
  }
}

void
NNTP :: post (const StringView  & msg,
              Listener          * l)
{
  _listener = l;

  std::string s (msg.str, msg.len);
  if (s.empty() || s[s.size()-1]!='\n') s += '\n';
  replace_linear (s, "\n.", "\n..");
  replace_linear (s, "\n", "\r\n");
  replace_linear (s, "\r\r\n.", "\r\n");
  s += ".\r\n";

  // if we're in mute mode, don't post
  if (0)
  {
    std::cerr << LINE_ID
              << "Mute: Your Message won't be posted." << std::endl
              << "Your Message:" << std::endl
              << s << std::endl
              << "<end of message>" << std::endl;
  }
  else
  {
    _post = s;
    _commands.push_back ("POST\r\n");
    write_next_command ();
  }
}
