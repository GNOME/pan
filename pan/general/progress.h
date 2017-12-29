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

#ifndef __Progress_h__
#define __Progress_h__

#include <set>
#include <string>
#include <vector>
#include <pan/general/debug.h>
#include <pan/general/string-view.h>

extern "C"
{
    #include <stdint.h>
}

namespace pan
{
  class StringView;

  /**
   * Base class describing an object that goes through steps to some completion.
   *
   * It provides methods for telling interested parties how many steps are left,
   * what the Progress object is doing, and whether or not it completed successfully.
   *
   * @ingroup general
   */
  class Progress
  {
    public:

      /**
       * Interface class for those wanting to listen to changes in Progress objects.
       */
      struct Listener {
        Listener () {}
        virtual ~Listener () {}
        virtual void on_progress_pulse (Progress&) { }
        virtual void on_progress_step (Progress&, int percentage UNUSED) { }
        virtual void on_progress_status (Progress&, const StringView&) { }
        virtual void on_progress_error (Progress&, const StringView&) { }
        virtual void on_progress_finished (Progress&, int status UNUSED) { }
      };

    private:

      typedef std::set<Listener*> listeners_t;
      typedef Progress::listeners_t::const_iterator listeners_cit;
      listeners_t _listeners;

      void fire_pulse ();
      void fire_percentage (int p);
      void fire_status (const StringView& msg);
      void fire_error (const StringView& msg);
      void fire_finished (int status);

    protected:

      std::string _description; // used for default describe()
      std::string _status_text; // the last status text emitted
      std::vector<std::string> _errors; // the emitted error strings
      uint64_t _steps; // number of steps total
      uint64_t _step; // number of steps completed so far
      int _done; // value of set_finished()
      bool _active;

    public:

      void pulse ();
      void set_status (const StringView& status);
      void set_status_va (const char * fmt, ...);
      void set_error (const StringView& error);
      void init_steps (int steps);
      void add_steps (int steps);
      void increment_step (int increment=1);
      int get_progress_of_100 () const;
      void set_step (int step);
      void set_finished (int status);
      virtual std::string describe () const;
      std::string get_status () const { return _status_text; }

      void add_listener (Listener*);
      void remove_listener (Listener*);

    public:
      Progress (const StringView& description = StringView());
      virtual ~Progress ();
  };
}

#endif
