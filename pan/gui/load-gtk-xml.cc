#include <cassert>
#include <glib.h>
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "pan/general/debug.h"
#include "pan/gui/load-gtk-xml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n-lib.h>

namespace pan {

// load gtk xml file. This functions succeeds or die
void load_gtk_xml(GtkBuilder *builder, gchar const *file_name) {
  // try local gtk_xml
  bool ret = load_gtk_xml_from_path(builder, file_name, "pan/gui/xml");
  if (ret) {
    return;
  }

  // try system gtk_xml
  ret = load_gtk_xml_from_path(builder, file_name, PAN_SYSTEM_GTK_XML_PATH);
  if (ret) {
    return;
  }

  std::cerr << "Unable to load " << file_name
            << " gtk_xml. Use --debug flag for more details" << std::endl;
  assert(0);
}

bool load_gtk_xml_from_path(GtkBuilder *builder, gchar const *file_name,
                            gchar const *gtk_xml_dir) {
  GError *error = NULL;
  gchar *gtk_xml_path = g_build_filename(gtk_xml_dir, file_name, NULL);

  auto ret = gtk_builder_add_from_file(builder, gtk_xml_path, &error);

  if (ret == 0) {
    pan_debug("Unable to load gtk_xml " << file_name << " from " << gtk_xml_dir
                                        << ": " << error->message);
    g_error_free(error);
  }

  g_free(gtk_xml_path);
  return ret != 0;
}

} // namespace pan
