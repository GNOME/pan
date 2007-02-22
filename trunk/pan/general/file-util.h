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


#ifndef _FileUtil_h_
#define _FileUtil_h_

#include <string>
extern "C" {
  #include <stddef.h>
  #include <stdio.h>
  #include <glib/gtypes.h>
  #include <glib.h>
}
#include <pan/general/string-view.h>

#if GLIB_CHECK_VERSION(2,6,0)
extern "C" {
  #include <glib/gstdio.h>
}
#define g_freopen freopen
#define g_fopen fopen
#define g_rmdir rmdir
#define g_remove remove
#define g_unlink unlink
#define g_lstat lstat
#define g_stat stat
#define g_rename rename
#define g_open open
#endif

namespace pan
{
  /**
   * Collection of file utilities.
   *
   * @ingroup general
   */
  namespace file
  {
    /** just like strerror but never returns NULL */
    const char * pan_strerror (int error_number);

    /**
     * Returns the home pan directory, which falls back to $HOME/.pan2
     * if the PAN_HOME environmental variable isn't set.
     */
    std::string get_pan_home ();

    /**
     * If the specified directory doesn't exist, Pan tries to create it.
     * @param path
     * @return true if the directory already existed or was created; false otherwise
     */
    bool ensure_dir_exists (const StringView& path);

    /**
     * Makes a unique filename given an optional path and a starting file name.
     * The filename is sanitized before looking for uniqueness.
     */
    gchar* get_unique_fname( const gchar *path, const gchar *fname);

    /**
     * Attempt to make a filename safe for use.
     * This is done by replacing illegal characters with '_'.
     * This function assumes the input is UTF8 since gmime uses UTF8 interface.
     * return value must be g_free'd.
     */
    gchar* sanitize (const gchar *fname);

    /**
     * Check to see if the specifiled file exists.
     * @return true if the file exists, false otherwise
     * @param filename the file to check
     */
    bool file_exists (const char * filename);

    /**
     * Removes extra '/' characters from the specified filename
     * @param filename
     * @return the filename pointer.
     */
    char* normalize_inplace (char * filename);
  };
}

#endif // _FileUtil_h_
