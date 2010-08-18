#ifndef __PAN_XFACE_H__
#define __PAN_XFACE_H__

#include <gdk/gdk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

G_BEGIN_DECLS

/** @ingroup GUI */
extern GdkPixmap*   pan_gdk_pixmap_create_from_x_face (GdkDrawable*, const char *);

G_END_DECLS

#endif
