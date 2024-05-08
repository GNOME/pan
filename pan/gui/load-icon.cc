#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "load-icon.h"
#include "pan/general/debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n-lib.h>

namespace pan {

GdkPixbuf *load_icon(gchar const *file_name)
{
  // try local icon
  GdkPixbuf *pixbuf = load_icon_from_path(file_name, "pan/icons");

  if (pixbuf != nullptr)
  {
    return pixbuf;
  }

  // try system icon
  return load_icon_from_path(file_name, PAN_SYSTEM_ICON_PATH);
}

GdkPixbuf *load_icon_from_path(const gchar *file_name, const gchar *icon_dir)
{
  GError *error = NULL;
  gchar *icon_path = g_build_filename(icon_dir, file_name, NULL);
  GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(icon_path, &error);

  if (error != NULL)
  {
      fprintf(stderr,
           "Warning: Unable to load icon %s from %s: %s\n",
           file_name,
           icon_dir,
           error->message);

      g_error_free(error);
  }

  g_free(icon_path);
  return pixbuf;
}

} // namespace pan
