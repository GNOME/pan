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


#ifndef _FileUtil_h_
#define _FileUtil_h_

#include <string>
extern "C" {
  #include <stddef.h>
  #include <stdio.h>
}
#include <glib.h>
#include <pan/general/string-view.h>


namespace pan
{
  /**
   * Collection of file utilities.
   *
   * @ingroup general
   */
  namespace file
  {
    /** Stats a file and prints out some useful info. Umask etc.... */
    std::ostream& print_file_info (std::ostream&, const char*);

    /** just like strerror but never returns NULL */
    const char * pan_strerror (int error_number);

    /**
     * Returns the home pan directory, which falls back to $HOME/.pan2
     * if the PAN_HOME environmental variable isn't set.
     */
    const std::string & get_pan_home ();

    /**
	 * Returns an absolute filename of a file
	 */
    std::string absolute_fn(const std::string &dir, const std::string &base);

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
     * <ol>
     * <li>Replacing illegal characters with '_'.
     * <li>Ensure the resulting string is UTF8-safe
     * <li>This function assumes the input is UTF8 since gmime uses UTF8 interface.
     * <li>Return value must be g_free'd.
     * </ol>
     */
    std::string sanitize (const StringView& filename);

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

     /**
      * Given the location of a text file, read it in and massage it
      * to the point where it should be usable -- convert CR/LF to LF,
      * and ensure that it's UTF8-clean.
      */
     bool get_text_file_contents (const StringView  & filename,
                                  std::string       & setme,
                                  const char        * fallback_charset_1=nullptr,
                                  const char        * fallback_charset_2=nullptr);

  };
}

#endif // _FileUtil_h_
