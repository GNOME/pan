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

#ifndef __EditorSpawner_h__
#define __EditorSpawner_h__

#include <functional>
#include <stdexcept>

namespace pan {

  class Prefs;

  class EditorSpawner {
    public:
      typedef std::function<void(int, char *)> Callback;

      /** Spawns an editor for the named file.
       *
       * The callback is actioned when the editor window is closed, and it
       * should take care of popping the correct window back to the front if
       * required.
       *
       * If there's an error, an exception is thrown and an error message is
       * logged.
       */
      EditorSpawner(char *fname,
                    Callback callback,
                    Prefs const & prefs);

      /** Destructor does some cleanup */
      ~EditorSpawner();

    private:
      unsigned int _child_id;
  };

  class EditorSpawnerError : public std::runtime_error
  {
    public:
        EditorSpawnerError();
  };

}

#endif
