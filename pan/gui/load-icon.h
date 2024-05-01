#ifndef E_LOAD_ICON_H
#define E_LOAD_ICON_H

#include <gtk/gtk.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

namespace pan {
    GdkPixbuf* load_icon(const char *file);
    GdkPixbuf* load_icon_from_path(const char *file, const gchar* path);
}

G_END_DECLS

#endif /* E_LOAD_ICON_H */
