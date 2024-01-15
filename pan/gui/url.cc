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
#include <iostream>
#include <string>
#include <glib.h>
#include <glib/gi18n.h>
#include <pan/general/log.h>
#include "url.h"

using namespace pan;

// possible values: "gnome", "kde", "mac", "windows", "custom"
const char*
URL :: get_environment ()
{
  const char * mode ("custom");
#if defined(G_OS_WIN32)
  mode = "windows";
#elif defined(G_OS_DARWIN)
  mode = "mac";
#else // not mac or windows
  if (g_getenv ("GNOME_DESKTOP_SESSION_ID"))
    mode = "gnome";
  else if (g_getenv ("KDE_FULL_SESSION"))
    mode = "kde";
#endif
  return mode;
}

void
URL :: get_default_editors (std::set<std::string>& editors)
{
  editors.clear ();

  const std::string environment = URL :: get_environment ();
  if (environment == "windows")
  {
    editors.insert ("notepad");
    editors.insert ("notepad2");
    editors.insert ("pfe");
  }
  else if (environment == "mac")
  {
    editors.insert ("edit -w");
    editors.insert ("TextEdit");
  }
  else if (environment == "kde")
  {
    editors.insert ("kate");
    editors.insert ("kwrite");
  }
  else // gnome and default
  {
    editors.insert ("gedit");
    editors.insert ("gvim -f");
    editors.insert ("xterm -e vim");
  }
}


void
URL :: open (const Prefs& prefs, const char * url, Mode mode)
{
  g_return_if_fail (url && *url);

  std::string tmp;

  if (mode==AUTO) {
    if (strstr(url,"mailto"))
      mode = MAIL;
    else if (strstr(url, "gemini://"))
      mode = GEMINI;
    else if (strstr(url,"http") || strstr(url,"://"))
      mode = WEB;
    else // ...
      mode = WEB;
  }

  if ((mode==AUTO || mode==WEB) && !strstr (url, "://") && strstr (url, "www")) {
    mode = WEB;
    tmp = std::string("http://") + url;
  } else if ((mode==AUTO || mode==MAIL) && !strstr (url, "://") && strchr (url, '@') && !strstr(url,"mailto:")) {
    mode = MAIL;
    tmp = std::string("mailto:") + url;
  } else {
    tmp = url;
  }

  const char * mode_key;
  const char * custom_key;
  const char * custom_fallback;

  switch (mode) {
      case MAIL:
          mode_key = "mailer-mode";
          custom_key = "custom-mailer";
          custom_fallback = "thunderbird";
          break;
      case GEMINI:
          mode_key = "gemini-mode";
          custom_key = "custom-gemini";
          custom_fallback = "lagrange";
          break;
      case WEB:
      default:
          mode_key = "browser-mode";
          custom_key = "custom-browser";
          custom_fallback = "firefox";
          break;
  }

  std::string cmd;
  const std::string env (prefs.get_string (mode_key, get_environment()));
       if (env == "gnome")   cmd = "xdg-open";
  else if (env == "kde")     cmd = "kfmclient exec";
  else if (env == "mac")     cmd = "open";
  else if (env == "windows") cmd = "rundll32 url.dll,FileProtocolHandler";
  else                       cmd = prefs.get_string (custom_key, custom_fallback);

  cmd += std::string(" \"") + tmp + '"';
  // std::cerr << __FILE__ << ':' << __LINE__ << " cmd [" << cmd << ']' << std::endl;
  GError * err (nullptr);
  g_spawn_command_line_async (cmd.c_str(), &err);
  if (err != NULL) {
    Log::add_err_va (_("Error starting URL: %s (Command was: %s)"), err->message, cmd.c_str());
    g_clear_error (&err);
  }
}
