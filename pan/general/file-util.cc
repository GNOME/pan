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
#include <cerrno>
#include <cctype>

extern "C"
{
  #include <unistd.h>
  #include <sys/stat.h>
  #include <sys/types.h>

  #include <glib.h>
  #include <glib/gi18n.h>
}

#include "log.h"
#include "messages.h"
#include "file-util.h"
#include "utf8-utils.h"

using namespace pan;

#define is_nonempty_string(a) ((a) && (*a))

/***
****
***/

std::string
file :: get_pan_home ()
{
  static std::string pan_home;
  if (pan_home.empty())
  {
    const char * env_str = g_getenv ("PAN_HOME");
    if (env_str && *env_str)
      pan_home = env_str;
    else {
      char * pch = g_build_filename (g_get_home_dir(), ".pan2", NULL);
      pan_home = pch;
      g_free (pch);
    }
  }
                                                                                                                                
  file :: ensure_dir_exists (pan_home);
  return pan_home;
}

const char*
file :: pan_strerror (int error_number)
{
  const char * pch (g_strerror (error_number));
  return pch && *pch ? pch : "";
}

#if !GLIB_CHECK_VERSION(2,8,0)
static int
pan_mkdir (const char * path, gulong mode)
{
  int retval = 0;

  pan_return_val_if_fail (is_nonempty_string(path), retval);

  if (strlen(path)==2 && isalpha((guchar)path[0]) && path[1]==':')
  {
    /* smells like a windows pathname.. skipping */
  }
  else
  {
    errno = 0;
#if GLIB_CHECK_VERSION(2,6,0)
    retval = g_mkdir (path, mode);
#elif defined(G_OS_WIN32)
    retval = mkdir (path);
#else
    retval = mkdir (path, mode);
#endif
    if (errno == EEXIST)
      retval = 0;
  }
	
  return retval;
}
#endif


bool
file :: ensure_dir_exists (const StringView& dirname_sv)
{
  assert (!dirname_sv.empty());

  pan_return_val_if_fail (!dirname_sv.empty(), true);
  bool retval (true);

  const std::string dirname (dirname_sv.to_string());
  if (!g_file_test (dirname.c_str(), G_FILE_TEST_IS_DIR))
  {
#if GLIB_CHECK_VERSION(2,8,0)
    retval = !g_mkdir_with_parents (dirname.c_str(), 0755);
#else
    const char * in = dirname.c_str();
    char * buf = g_new (char, dirname.size()+1);
    char * out = buf;

    *out++ = *in++;
    for (;;)
    {
      if (*in=='\0' || *in==G_DIR_SEPARATOR)
      {
        *out = '\0';

        if (!g_file_test (buf, G_FILE_TEST_IS_DIR))
        {
          Log :: add_info_va (_("Creating directory \"%s\""), buf);
          if (pan_mkdir (buf, 0755))
          {
            Log::add_err_va (_("Couldn't create directory \"%s\": %s"), buf, pan_strerror (errno));
            retval = FALSE;
            break;
          }
        }
      }

      *out = *in;
      if (!*in)
        break;

      ++out;
      ++in;
    }

    g_free (buf);
#endif
  }

  return retval;
}

bool
file :: file_exists (const char * filename)
{
   return filename && *filename && g_file_test (filename, G_FILE_TEST_EXISTS);
}

/**
*** Attempt to make a filename safe for use.
*** This is done by replacing illegal characters with '_'.
*** This function assumes the input is UTF8 since gmime uses UTF8 interface.
*** return value must be g_free'd.
**/
char*
file :: sanitize (const char *fname)
{
	/* characters to exclude from name */
#ifdef G_OS_WIN32
	static const gunichar excl[]={':','?','*','"','\'','/','<','>','|','/','\\',0};
#else
	static const gunichar excl[]={'/', '\\', 0};
#endif
	gunichar *name_32, *p;
	glong i, len;
	char *retval;

	/* sanity checks */
	pan_return_val_if_fail(fname!=NULL,NULL);

	/* convert to unicode for easy access */
	name_32 = g_utf8_to_ucs4_fast (fname, -1, &len);
	
	/* strip illegal characters */
	for(p=name_32, i=0; i!=len; i++, p++) {
		gunichar c=*p;
		if(g_unichar_isalnum(c)) continue;
		if(g_unichar_isspace(c)) continue;
		if(g_unichar_ispunct(c)) {
			const gunichar *t=excl;
			for (;*t; t++)
				if(c==*t) {
					*p='_';
					break;
				}
		}
	}

	/* back to UTF8 */
	retval = g_ucs4_to_utf8 (name_32, -1, NULL, NULL, NULL);

	/* cleanup */
	g_free(name_32);

	return retval;
}

char*
file :: normalize_inplace (char * filename)
{
  register char *in, *out;
  pan_return_val_if_fail (filename && *filename, filename);

  for (in=out=filename; *in; )
    if (in[0]==G_DIR_SEPARATOR && in[1]==G_DIR_SEPARATOR)
      ++in;
    else
      *out++ = *in++;
  *out = '\0';

  return filename;
}

/**
*** Makes a unique filename given an optional path and a starting file name.
*** The filename is sanitized before looking for uniqueness.
**/
char*
file :: get_unique_fname ( const gchar *path, const gchar *fname)
{
   /* sanity checks */
   pan_return_val_if_fail (is_nonempty_string (fname), NULL);

   /* sanitize filename */
   char * temp_fn = sanitize (fname);
   GString * filename = g_string_new (temp_fn);
   g_free(temp_fn);

   /* add the directory & look for uniqueness */
   const char * front = filename->str;
   const char * lastdot = strrchr (front, '.');
   char * lead;
   char * tail;
   if (lastdot == NULL) {
      lead = g_strdup (front);
      tail = g_strdup ("");
   } else {
      lead = g_strndup (front, lastdot-front);
      tail = g_strdup (lastdot);
   }

   for (int i=1;; ++i)
   {
      char * unique;

      if (i==1 && is_nonempty_string(path))
      {
         unique = g_strdup_printf ("%s%c%s%s",
                             path, G_DIR_SEPARATOR,
                             lead, tail);
      }
      else if (i==1)
      {
         unique = g_strdup_printf ("%s%s", lead, tail);
      }
      else if (is_nonempty_string(path))
      {
         unique = g_strdup_printf ("%s%c%s_copy_%d%s",
                             path, G_DIR_SEPARATOR,
                             lead, i, tail);
      }
      else
      {
         unique = g_strdup_printf ("%s_copy_%d%s", lead, i, tail);
      }

      if (!file_exists (unique)) {
         g_string_assign (filename, unique);
         g_free (unique);
         break;
      }

      g_free (unique);
   }

   /* cleanup */
   g_free (lead);
   g_free (tail);

   return normalize_inplace (g_string_free (filename, FALSE));
}

/***
****
***/

bool
file :: get_text_file_contents (const StringView  & filename,
                                std::string       & setme,
                                const char        * fallback_charset_1,
                                const char        * fallback_charset_2)
{
  // read in the file...
  char * body (0);
  gsize body_len (0);
  GError * err (0);
  const std::string fname (filename.str, filename.len);
  g_file_get_contents (fname.c_str(), &body, &body_len, &err);
  if (err) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), err->message, g_strerror(errno));
    g_clear_error (&err);
    return false;
  }

  // CRLF => LF
  body_len = std::remove (body, body+body_len, '\r') - body;

  // utf8
  setme = content_to_utf8  (StringView (body, body_len),
                            fallback_charset_1,
                            fallback_charset_2);

  return true;
}
