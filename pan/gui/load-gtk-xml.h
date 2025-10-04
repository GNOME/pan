#ifndef E_LOAD_GTK_XML_H
#define E_LOAD_GTK_XML_H

#include <gtk/gtk.h>
#include <gdk/gdk.h>

G_BEGIN_DECLS

namespace pan {
    void load_gtk_xml(GtkBuilder *builder,const char *file);
    bool load_gtk_xml_from_path(GtkBuilder *builder, const char *file, const gchar* path);
}

G_END_DECLS

#endif /* E_LOAD_GTK_XML_H */
