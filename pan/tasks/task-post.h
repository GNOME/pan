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

#ifndef __TaskPost_h__
#define __TaskPost_h__

#include <gmime/gmime-message.h>
#include <pan/general/quark.h>
#include <pan/data/data.h>
#include <pan/tasks/task.h>
#include <pan/tasks/nntp.h>

namespace pan
{
  /**
   * Task for posting an article.
   * @ingroup tasks
   */
  class TaskPost: public Task, private NNTP::Listener
  {
    public: // life cycle
      TaskPost (const Quark& server, GMimeMessage * message);
      virtual ~TaskPost ();

    public: // Task's virtual functions
      unsigned long get_bytes_remaining () const override { return 0; }
      GMimeMessage* get_message () { return _message; }

    protected: // Task's virtual functions
      void use_nntp (NNTP * nntp) override;

    private: // NNTP::Listener's virtual functions
      void on_nntp_done (NNTP*, Health, const StringView&) override;

    private: // implementation
      Quark _server;
      GMimeMessage * _message;
  };
}

#endif
