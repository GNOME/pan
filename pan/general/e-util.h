/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * e-util.h
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

#ifndef _E_UTIL_H_
#define _E_UTIL_H_

#include <stddef.h>
#include <time.h>

namespace pan {

class EvolutionDateMaker
{
  private:
    char * locale_recent;
    char * locale_today;
    char * locale_this_week;
    char * locale_this_year;
    char * locale_old;
    bool am_pm_are_defined_in_locale;

  private:
    size_t e_strftime_fix_am_pm (char *s, size_t max,
                                 const char *fmt, const struct tm *tm) const;
    size_t e_utf8_strftime_fix_am_pm (char *s, size_t max,
                                      const char *locale_fmt, const struct tm *tm) const;

  private:
    time_t now_time;
    struct tm now_tm;
    struct tm last_seven_days[7]; // [0=yesterday ... 6=one week ago]

  public:
    EvolutionDateMaker (time_t n=time(nullptr));
    ~EvolutionDateMaker ();
    char* get_date_string (time_t date) const;
};

}

#endif /* _E_UTIL_H_ */
