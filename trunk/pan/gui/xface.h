#ifndef __PAN_XFACE_H__
#define __PAN_XFACE_H__

#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

/** @ingroup GUI */
extern GdkPixbuf*   pan_gdk_pixbuf_create_from_x_face (GdkColormap*, GdkDrawable*, const char *);

G_END_DECLS

#endif
