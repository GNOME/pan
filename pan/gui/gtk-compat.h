//========================================================================
/**@file	pan/pan/gui/gtk-compat.h
 * @author imhotep
 * @author	kid
 * @date
 * 	Created:	Mon 09 May 2011 04:42:46 PM MDT \n
 * 	Last Update:	Sun 08 Jan 2012 11:56:00 PM GMT
 */
/*------------------------------------------------------------------------
 * Description:	GTK Compatibility layer for GTK2/3+ migration
 *
 *========================================================================
 */

#ifndef PAN_GTK_COMPAT_H
#define PAN_GTK_COMPAT_H

#include <gtk/gtk.h>
#include <gdk/gdk.h>

#ifdef __cplusplus
namespace
{
#endif


  static inline void window_get_pointer (GdkEventMotion* event, int* x, int* y, GdkModifierType* t)
  {
    gdk_window_get_device_position (event->window, event->device, x, y, t);
  }

  // include this for conversion of old key names to new
  #include <gdk/gdkkeysyms-compat.h>

  #define GTK_OBJECT(w) w
  typedef GtkWidget GtkObject;

  static inline void cursor_unref(GdkCursor *p)
  {
    g_object_unref(p);
  }

#ifdef __cplusplus
}
#endif

#endif
