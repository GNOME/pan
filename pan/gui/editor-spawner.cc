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

#include "editor-spawner.h"

#include <pan/general/log.h>
#include <pan/gui/prefs.h>
#include <pan/gui/url.h> //For get_default_editors

#include <glib/gi18n.h> // for _

#include <set>

namespace pan {

namespace
{
	struct se_data
	{
		char *fname;
		EditorSpawner::Callback callback;
	};

	void child_watch_callback(GPid pid, int status, void * data)
	{
		se_data * d{static_cast<se_data *>(data)};
		g_spawn_close_pid(pid);
		d->callback(status, d->fname);
		delete d;
	}
}

EditorSpawner::EditorSpawner(
  char * fname,
  Callback callback,
  Prefs const & prefs)
{
  // Get the configured editor
  std::set<std::string> editors;
  URL :: get_default_editors(editors);
  std::string const editor{prefs.get_string("editor", *editors.begin())};

  //Create the command line
  int argc{0};
  char ** argv{nullptr};
  GError * err{nullptr};
  g_shell_parse_argv(editor.c_str(), &argc, &argv, &err);
  if (err != nullptr) {
    Log::add_err_va(
      _("Error parsing \"external editor\" command line: %s (Command was: %s)"),
      err->message,
      editor.c_str());
    g_clear_error (&err);
    throw EditorSpawnerError();
  }

  // put temp file's name into the substitution
  bool filename_added{false};
  for (int i = 0; i < argc; i += 1) {
    char * token{argv[i]};
    char * sub{strstr (token, "%t")};
    if (sub) {
      GString * gstr{g_string_new (nullptr)};
      g_string_append_len(gstr, token, sub - token);
      g_string_append(gstr, fname);
      g_string_append(gstr, sub + 2);
      g_free(token);
      argv[i] = g_string_free(gstr, false);
      filename_added = true;
    }
  }

  // no substitution field -- add the filename at the end
  if (not filename_added) {
    char ** v{g_new(char*, argc + 2)};
    for (int i = 0; i < argc; i += 1) {
      v[i] = argv[i];
    }
    v[argc] = g_strdup(fname);
    argc += 1;
    v[argc] = nullptr;
    g_free(argv);
    argv = v;
  }

  // spawn off the external editor
  GPid pid;
  g_spawn_async(nullptr, argv, nullptr,
                (GSpawnFlags)(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
                nullptr, nullptr, &pid, &err);
  g_strfreev(argv);

  if (err != nullptr) {
    Log::add_err_va(_("Error starting external editor: %s"), err->message);
    g_clear_error(&err);
    throw EditorSpawnerError();
  }

  se_data *data = new se_data;
  data->fname = fname;
  data->callback = callback;

  _child_id = g_child_watch_add(pid, child_watch_callback, data);
}

EditorSpawner::~EditorSpawner()
{
  g_source_remove(_child_id);
}

EditorSpawnerError::EditorSpawnerError() :
  runtime_error("Error spawning editor - see log")
{
}

}
