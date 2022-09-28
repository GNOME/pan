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

#ifndef __NNTP_h__
#define __NNTP_h__

#include <stdint.h>
#include <deque>
#include <pan/general/macros.h> // for UNUSED
#include <pan/general/quark.h>
#include <pan/general/string-view.h>
#include <pan/tasks/health.h>
#include <pan/tasks/socket.h>
#include <pan/data/data.h>

namespace pan {


  /**
   * Asynchronously processes NNTP protocol commands.
   *
   * @ingroup tasks
   */
  class NNTP: private Socket::Listener
  {
    public:

      enum {
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

        FEATURE_ENABLED            = 290,

        SEND_ARTICLE_NOW           = 340,
        NO_POSTING                 = 440,
        POSTING_FAILED             = 441,
        DUPE_ARTICLE               = 435,  // sent additionally to 441

        TOO_MANY_CONNECTIONS       = 400,

        NO_GROUP_SELECTED          = 412,
        NO_SUCH_ARTICLE_NUMBER     = 423,
        NO_SUCH_ARTICLE            = 430,

        ERROR_CMD_NOT_UNDERSTOOD   = 500,
        ERROR_CMD_NOT_SUPPORTED    = 501,
        NO_PERMISSION              = 502,
        FEATURE_NOT_SUPPORTED      = 503
      };

      /**
       * Base class for objects that listen for NNTP events.
       *
       * NNTP responses are lists of lines (like on XOVER and
       * ARTICLE requests) and one-line status messages.
       * these correspond to on_nntp_line() and on_nntp_done().
       * A special case is made for the GROUP command's response
       * so the client can get back the group's information fields.
       *
       * @ingroup tasks
       * @see NNTP
       */
      struct Listener
      {
        Listener () {}

        virtual ~Listener () {}

        /**
         * Invoked for each line of an NNTP server's list responses,
         * such as a list of headers for an XOVER command or a list of
         * lines for an ARTICLE command.
         */
        virtual void on_nntp_line  (NNTP               * nntp UNUSED,
                                    const StringView   & line UNUSED)  {}

        /**
         * Called at the end of an NNTP command.  If the command was
         * one that produced a list, on_nntp_line() may have been
         * called before this.
         *
         * When this is called, the listener can safely clean up
         * anything associated with processing the command.
         *
         * @param health returns OK, ERR_NETWORK, or ERR_SERVER.
         *               ERR_LOCAL is never used here.
         */
        virtual void on_nntp_done  (NNTP               * nntp     UNUSED,
                                    Health               health   UNUSED,
                                    const StringView   & response UNUSED) {}

        virtual void on_xover_done  (NNTP               * nntp     UNUSED,
                                    Health               health   UNUSED,
                                    const StringView   & response UNUSED) {}

        /**
         * Called whenever an NNTP object sets the current group.
         */
        virtual void on_nntp_group (NNTP               * nntp          UNUSED,
                                    const Quark        & group         UNUSED,
                                    Article_Count        estimated_qty UNUSED,
                                    Article_Number       low           UNUSED,
                                    Article_Number       high
                                           UNUSED) {}

       };

      public:

        NNTP (const Quark        & server,
              const std::string  & username,
              const std::string  & password,
              ServerInfo         & info,
              Socket             * socket):
          _server(server),
          _server_info(info),
          _socket(socket),
          _socket_error(false),
          _listener(nullptr),
          _username(username),
          _password(password),
          _nntp_response_text(false),
          _compression(false)
       {}

       virtual ~NNTP ()
       {}

    public:

      /* Internal only */
      void enter_group (const Quark& group);
      void get_headers (const Quark & group, const char * message_id, Listener  * l);
      void get_headers (const Quark & group, Article_Number article_number, Listener * l);
      void get_body (const Quark & group, const char * message_id, Listener  * l);
      void get_body (const Quark & group, Article_Number article_number, Listener * l);
      /**
       * Lists all available commands.
       */
      void help (Listener * l);

      /**
       * Executes a handshake command.
       *
       * This is actually an empty string, but the
       * news server will respond to it.
       */
      void handshake        (Listener           * l);

      /**
       * Executes an XOVER command: "XOVER low-high"
       *
       * If successful, this will invoke Listener::on_nntp_line()
       * for each article header line we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void xover            (const Quark        & group,
                             Article_Number       low,
                             Article_Number       high,
                             Listener           * l);

      void xzver            (const Quark        & group,
                             Article_Number       low,
                             Article_Number       high,
                             Listener           * l);

      /**
       * Executes an XOVER command: "XOVER" to count
       * the xover numbers internally
       *
       * If successful, this will invoke Listener::on_nntp_line()
       * for each article header line we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void xover_count_only (const Quark        & group,
                             Listener           * l);

      /**
       * Executes a LIST command: "LIST"
       *
       * If successful, this will invoke Listener::on_nntp_line()
       * for each newsgroup we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void list             (Listener           * l);

      /**
       * Executes a LIST command: "LIST GROUPS"
       *
       * If successful, this will invoke Listener::on_nntp_line()
       * for each newsgroup we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void list_newsgroups  (Listener           * l);

      /**
       * Executes an ARTICLE command: "ARTICLE article_number"
       *
       * If the NNTP's state isn't currently in the right group,
       * a group() command will be called first.
       *
       * If successful, this will invoke Listener::on_nntp_line()
       * for each line of the article we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.  It will only be called
       * once, at the end of the article() command, even if we had
       * to change groups.
       */
      void article          (const Quark        & group,
                             Article_Number     article_number,
                             Listener           * l);

      /**
       * Executes an ARTICLE command: "ARTICLE message-id"
       *
       * If the NNTP's state isn't currently in the right group,
       * a group() command will be called first.
       *
       * If successful, this will invoke Listener::on_nntp_line()
       * for each line of the article we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.  It will only be called
       * once, at the end of the article() command, even if we had
       * to change groups.
       */
      void article          (const Quark        & group,
                             const char         * message_id,
                             Listener           * l);

      /**
       * Executes an GROUP command: "GROUP groupname"
       *
       * If successful, this will invoke Listener::on_nntp_group().
       * for each line of the article we get back.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void group            (const Quark        & group,
                             Listener           * l);

      /**
       * Executes a QUIT command: "QUIT"
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void goodbye          (Listener           * l);

      /**
       * Executes a short, non-state-changing command ("MODE READER")
       * to keep the session from timing out.
       *
       * Listener::on_nntp_done() will be called whether the
       * command is successful or not.
       */
      void noop             (Listener           * l);

      void post             (const StringView   & message,
                             Listener           * l);

      void cancel           (const Quark        & message_id,
                             Listener           * l);

    public:

      const Quark _server;
      ServerInfo& _server_info;
      Quark _group;
      Socket * _socket;
      bool _socket_error;
      const std::string& get_username()
      {
        return _username;
      }

    protected:

      Listener * _listener;
      /** Kept in case the server gives prompts us for it. */
      const std::string _username;
      /** Kept in case the server gives prompts us for it. */
      const std::string _password;
      /** Used to remember the article send in via post(). */
      std::string _post;
      /** True if the server told us that we're getting a list back. */
      bool _nntp_response_text;

      typedef std::deque<std::string> strings_t;
      strings_t _commands;
      std::string _previous_command;
      void write_next_command ();

      void fire_done_func (Health, const StringView& response);

    public:
      /** session flag for compression (gzip-style) (giganews etc.)*/
      bool _compression;

    private: // private socket listener funcs, for socket handshake

      virtual bool on_socket_response (Socket*, const StringView& line);
      virtual void on_socket_error (Socket*);
      virtual void on_socket_abort (Socket*);

    public:

      /**
       * Interface class specifying how to dispose of an NNTP.
       */
      struct Source {
        Source () {}
        virtual ~Source () {}
        virtual void check_in (NNTP*, Health health) = 0;
      };
  };
}

#endif /* __NNTP_H__ */
