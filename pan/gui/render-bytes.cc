#include <glib/gutils.h> // g_snprintf
#include "render-bytes.h"

namespace pan
{
  char*
  render_bytes (guint64 bytes)
  {
    static const unsigned long KIBI (1024ul);
    static const unsigned long MEBI (1048576ul);
    static const unsigned long GIBI (1073741824ul);
    static char buf[128];

    if (bytes < KIBI)
      g_snprintf (buf, sizeof(buf), "%d B", (int)bytes);
    else if (bytes < MEBI)
      g_snprintf (buf, sizeof(buf), "%.0f KiB", (double)bytes/KIBI);
    else if (bytes < GIBI)
      g_snprintf (buf, sizeof(buf), "%.1f MiB", (double)bytes/MEBI);
    else
      g_snprintf (buf, sizeof(buf), "%.2f GiB", (double)bytes/GIBI);

    return buf;
  }
}
