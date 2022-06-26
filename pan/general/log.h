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

#ifndef __Log_h__
#define __Log_h__

#include <ctime>
#include <set>
#include <string>
#include <deque>
#include <pan/general/singleton-template.h>
#include <pan/general/macros.h>

namespace pan
{
  /**
   * Logs information and error messages that that the user should see.
   */
  class Log : public PanSingleton< Log >
  {
    public:
      enum Severity {
        PAN_SEVERITY_INFO = 1,
        PAN_SEVERITY_ERROR = 2,
        PAN_SEVERITY_URGENT = (1<<10)
      };



      /**
       * A log message specifying the message's text, severity, and time.
       * @see Log
       * @ingroup general
       */
      struct Entry {
        time_t date;
        Severity severity;
        std::deque<Entry*> messages;
        std::string message;
        bool is_child;
        Entry() : date(0), severity(PAN_SEVERITY_INFO), is_child(false) { }
        virtual ~Entry () { foreach (std::deque<Entry*>, messages, it) delete *it; }
      };

      void add_entry(Entry& e, std::deque<Entry>& list);

      /** Interface class for objects that listen to a Log's events */
      struct Listener {
        Listener () {}
        virtual ~Listener () {}
        virtual void on_log_entry_added (const Entry& e) = 0;
        virtual void on_log_cleared () = 0;
      };

      typedef std::deque<Entry> entries_t;
      typedef std::deque<Entry*> entries_p;

    public:
      void add (Severity, const char *);
      void add_va (Severity, const char *, ...);
      const entries_t& get_entries () const { return _entries; }
      void clear ();
      void add_listener (Listener* l) { _listeners.insert(l); }
      void remove_listener (Listener* l) { _listeners.erase(l); }

    private:
      typedef std::set<Listener*> listeners_t;
      listeners_t _listeners;
      void fire_entry_added (const Entry& e);
      void fire_cleared ();
      entries_t _entries;

    public: // convenience functions
      static void add_info (const char * s) { Log::get().add (Log::PAN_SEVERITY_INFO, s); }
      static void add_info_va (const char *, ...);
      static void add_err (const char * s) { Log::get().add (Log::PAN_SEVERITY_ERROR, s); }
      static void add_err_va (const char *, ...);
      static void add_urgent (const char * s) { Log::get().add ((Severity)(PAN_SEVERITY_ERROR|PAN_SEVERITY_URGENT), s); }
      static void add_urgent_va (const char *, ...);
      static void entry_added (const Entry& e) { Log::get().fire_entry_added(e); }

      static void add_entry_list(Entry& e, std::deque<Entry>& list) { Log::get().add_entry (e, list); }
  };
}

#endif /* __Log_H__ */
