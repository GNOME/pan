#ifndef __Time_Elapsed_h__
#define __Time_Elapsed_h__

#include <glib.h> // for GTimeVal, g_get_current_time and GUSEC_PER_SEC

namespace pan
{
  /**
   * Simple object used for logging how long a piece of code takes to execute.
   *
   * Instantiate it before calling the code to be timed, then call
   * get_seconds_elapsed() after the code returns.
   *
   * @ingroup general
   */
  struct TimeElapsed
  {
    GTimeVal start;
    TimeElapsed() { g_get_current_time (&start); }
    double get_seconds_elapsed () const {
      GTimeVal finish;
      g_get_current_time (&finish);
      double diff = finish.tv_sec - start.tv_sec;
      diff += (finish.tv_usec - start.tv_usec)/(double)G_USEC_PER_SEC;
      return diff;
    }

    double get_usecs_elapsed () const {
      GTimeVal finish;
      g_get_current_time (&finish);
      double diff = finish.tv_usec - start.tv_usec;
      return diff;
    }
  };
}

#endif
