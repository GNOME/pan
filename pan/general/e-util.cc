/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* 
 * e-util.c
 * Copyright 2000, 2001, Ximian, Inc.
 *
 * Authors:
 *   Chris Lahey <clahey@ximian.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License, version 2, as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <ctime>
extern "C" {
  #include <ctype.h>
}
#include <glib.h>
#include <glib/gi18n.h>
#include "debug.h"
#include "e-util.h"

static char *
e_strdup_strip(const char *string)
{
	int i;
	int length = 0;
	int initial = 0;
	for ( i = 0; string[i]; i++ ) {
		if (initial == i && isspace((unsigned char) string[i])) {
			initial ++;
		}
		if (!isspace((unsigned char) string[i])) {
			length = i - initial + 1;
		}
	}
	return g_strndup(string + initial, length);
}

static size_t
e_strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
#ifdef HAVE_LKSTRFTIME
	return strftime(s, max, fmt, tm);
#else
	char *c, *ffmt, *ff;
	size_t ret;

	ffmt = g_strdup(fmt);
	ff = ffmt;
	while ((c = strstr(ff, "%l")) != NULL) {
		c[1] = 'I';
		ff = c;
	}

	ff = ffmt;
	while ((c = strstr(ff, "%k")) != NULL) {
		c[1] = 'H';
		ff = c;
	}

	ret = strftime(s, max, ffmt, tm);
	g_free(ffmt);
	return ret;
#endif
}

#if 0
static size_t 
e_utf8_strftime(char *s, size_t max, const char *fmt, const struct tm *tm)
{
	size_t sz, ret;
	char *locale_fmt, *buf;

	locale_fmt = g_locale_from_utf8(fmt, -1, NULL, &sz, NULL);
	if (!locale_fmt)
		return 0;

	ret = e_strftime(s, max, locale_fmt, tm);
	if (!ret) {
		g_free (locale_fmt);
		return 0;
	}

	buf = g_locale_to_utf8(s, ret, NULL, &sz, NULL);
	if (!buf) {
		g_free (locale_fmt);
		return 0;
	}

	if (sz >= max) {
		char *tmp = buf + max - 1;
		tmp = g_utf8_find_prev_char(buf, tmp);
		if (tmp)
			sz = tmp - buf;
		else
			sz = 0;
	}
	memcpy(s, buf, sz);
	s[sz] = '\0';
	g_free(locale_fmt);
	g_free(buf);
	return sz;
}
#endif

/**
 * Function to do a last minute fixup of the AM/PM stuff if the locale
 * and gettext haven't done it right. Most English speaking countries
 * except the USA use the 24 hour clock (UK, Australia etc). However
 * since they are English nobody bothers to write a language
 * translation (gettext) file. So the locale turns off the AM/PM, but
 * gettext does not turn on the 24 hour clock. Leaving a mess.
 *
 * This routine checks if AM/PM are defined in the locale, if not it
 * forces the use of the 24 hour clock.
 *
 * The function itself is a front end on strftime and takes exactly
 * the same arguments.
 *
 * TODO: Actually remove the '%p' from the fixed up string so that
 * there isn't a stray space.
 **/

size_t
EvolutionDateMaker :: e_strftime_fix_am_pm (char *s,
                                            size_t max,
                                            const char *fmt,
                                            const struct tm *tm) const
{
  size_t ret;

  if (am_pm_are_defined_in_locale || (!strstr(fmt,"%p") && !strstr(fmt,"%P")))
  {
    // either no AM/PM involved,
    // or it is involved and we can handle it
    ret = e_strftime (s, max, fmt, tm);
  }
  else // no am/pm defined... change to a 24 hour clock
  {
    char * ffmt = g_strdup (fmt);
    for (char * sp=ffmt; (sp=strstr(sp, "%l")); ++sp)
      sp[1]='H'; // maybe this should be 'k' but I've never seen a 24 clock actually use that format
    for (char * sp=ffmt; (sp=strstr(sp, "%I")); sp++)
      sp[1]='H';
    ret = e_strftime(s, max, ffmt, tm);
    g_free (ffmt);
  }
  return ret;
}

size_t 
EvolutionDateMaker :: e_utf8_strftime_fix_am_pm (char *s,
                                                 size_t max,
                                                 const char *locale_fmt,
                                                 const struct tm *tm) const
{
  size_t ret = e_strftime_fix_am_pm(s, max, locale_fmt, tm);
  if (!ret)
    return 0;

  gsize sz;
  char * buf = g_locale_to_utf8(s, ret, NULL, &sz, NULL);
  if (!buf)
    return 0;

  if (sz >= max) {
    char *tmp = buf + max - 1;
    tmp = g_utf8_find_prev_char (buf, tmp);
    sz = tmp ? tmp-buf : 0;
  }

  memcpy (s, buf, sz);
  s[sz] = '\0';
  g_free(buf);
  return sz;
}

#if !defined(HAVE_LOCALTIME_R)
#define localtime_r(a,b) *(b) = *localtime(a)
#endif

void
EvolutionDateMaker :: set_current_time (time_t now)
{
  now_time = now;
  localtime_r (&now_time, &now_tm);
  time_t tmp_time = now_time;
  const size_t secs_in_day = 60 * 60 * 24;
  for (int i=0;i<7; i++) {
    tmp_time -= secs_in_day;
    localtime_r (&tmp_time, &last_seven_days[i]);
  }
}

EvolutionDateMaker :: EvolutionDateMaker (time_t now)
{
  // build the locale strings
  locale_recent = g_locale_from_utf8 (_("%l:%M %p"), -1, NULL, NULL, NULL);
  locale_today = g_locale_from_utf8 (_("Today %l:%M %p"), -1, NULL, NULL, NULL);
  locale_this_week = g_locale_from_utf8 (_("%a %l:%M %p"), -1, NULL, NULL, NULL);
  locale_this_year = g_locale_from_utf8 (_("%b %d %l:%M %p"), -1, NULL, NULL, NULL);
  locale_old = g_locale_from_utf8 (_("%b %d %Y"), -1, NULL, NULL, NULL);

  // set the current time
  set_current_time (now);

  // test to see if am/pm symbols are defined in this locale
  char buf[10];
  *buf = '\0';
  e_strftime (buf, sizeof(buf), "%p", &now_tm);
  am_pm_are_defined_in_locale = *buf != '\0';
}

EvolutionDateMaker :: ~EvolutionDateMaker ()
{
  g_free (locale_old);
  g_free (locale_this_year);
  g_free (locale_this_week);
  g_free (locale_today);
  g_free (locale_recent);
}

char*
EvolutionDateMaker :: get_date_string (time_t then_time) const
{
  if (!then_time)
    return g_strdup (_("?"));

  struct tm then_tm;
  char buf[100];
  char *temp;
  bool done = false;

  localtime_r (&then_time, &then_tm);

  // less than eight hours ago...
  if (now_time - then_time < 60 * 60 * 8 && now_time > then_time) {
    e_utf8_strftime_fix_am_pm (buf, sizeof(buf), locale_recent, &then_tm);
    done = true;
  }

  // today...
  if (!done) {
    if (then_tm.tm_mday == now_tm.tm_mday &&
        then_tm.tm_mon  == now_tm.tm_mon &&
        then_tm.tm_year == now_tm.tm_year) {
      e_utf8_strftime_fix_am_pm (buf, sizeof(buf), locale_today, &then_tm);
      done = true;
    }
  }

  // this week...
  if (!done) {
    for (int i=0; i<6; ++i) {
      const struct tm& week_tm (last_seven_days[i]);
      if (then_tm.tm_mday == week_tm.tm_mday &&
          then_tm.tm_mon  == week_tm.tm_mon &&
          then_tm.tm_year == week_tm.tm_year) {
        e_utf8_strftime_fix_am_pm (buf, sizeof(buf), locale_this_week, &then_tm);
        done = true;
        break;
      }
    }
  }

  if (!done) {
    // this year...
    if (then_tm.tm_year == now_tm.tm_year)
      e_utf8_strftime_fix_am_pm (buf, sizeof(buf), locale_this_year, &then_tm);
    else // older than this year...
      e_utf8_strftime_fix_am_pm (buf, sizeof(buf), locale_old, &then_tm);
    temp = buf;
    while ((temp = strstr (temp, "  ")))
      memmove (temp, temp + 1, strlen (temp));
  }

  return e_strdup_strip (buf);
}
