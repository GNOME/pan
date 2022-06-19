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

#if !GTK_CHECK_VERSION(3, 0, 0)
  static inline GtkWidget* gtk_box_new (GtkOrientation orientation, int space)
  {
    GtkWidget* ret;
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
      ret = gtk_hbox_new (FALSE, space);
    else
      ret = gtk_vbox_new (FALSE, space);
    return ret;
  }
#endif

#if !GTK_CHECK_VERSION(3, 0, 0)
static inline GdkWindow * gdk_window_get_device_position (GdkWindow *window,
                                                           GdkDevice *device,
                                                           gint *x,
                                                           gint *y,
                                                           GdkModifierType *mask)
{
  return gdk_window_get_pointer (window, x, y, mask);
}
#endif

#if !GTK_CHECK_VERSION(3, 0, 0)
  static inline GtkWidget* gtk_paned_new(GtkOrientation orientation)
  {
    GtkWidget* ret;
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
      ret = gtk_hpaned_new();
    else
      ret = gtk_vpaned_new();
    return ret;
  }
#endif

#if !GTK_CHECK_VERSION(3, 0, 0)
  static inline GtkWidget* gtk_separator_new(GtkOrientation orientation)
  {
    GtkWidget* ret;
    if (orientation == GTK_ORIENTATION_HORIZONTAL)
      ret = gtk_hseparator_new();
    else
      ret = gtk_vseparator_new();
    return ret;
  }
#endif

  static inline void window_get_pointer (GdkEventMotion* event, int* x, int* y, GdkModifierType* t)
  {
#if !GTK_CHECK_VERSION(3, 0, 0)
    gdk_window_get_pointer (event->window, x, y, t);
#else
    gdk_window_get_device_position (event->window, event->device, x, y, t);
#endif
  }

#if defined(GTK_DISABLE_DEPRECATED) || GTK_CHECK_VERSION(3,0,0)
#if GTK_CHECK_VERSION(2,22,0)
#define GtkNotebookPage void
#endif
#endif

#if !GTK_CHECK_VERSION(3, 0, 0)
  #include <gdk/gdkkeysyms.h>
#endif

#if !GTK_CHECK_VERSION(3,0,0)

  typedef GtkStyle GtkStyleContext;

  static inline GtkStyleContext* gtk_widget_get_style_context(GtkWidget *w)
  {
    return gtk_widget_get_style(w);
  }

  static inline GtkIconSet* gtk_style_context_lookup_icon_set(GtkStyleContext *s,
      const char *id)
  {
    return gtk_style_lookup_icon_set(s,id);
  }

  static inline void gtk_widget_override_font(GtkWidget *w, PangoFontDescription *f)
  {
    gtk_widget_modify_font(w,f);
  }
#endif

#if GTK_CHECK_VERSION(3,0,0)
// include this for conversion of old key names to new
  #include <gdk/gdkkeysyms-compat.h>

  #define GTK_OBJECT(w) w
  typedef GtkWidget GtkObject;
#endif

  static inline void cursor_unref(GdkCursor *p)
  {
#if GTK_CHECK_VERSION(3,0,0)
    g_object_unref(p);
#else
    gdk_cursor_unref(p);
#endif
  }

#ifdef __cplusplus
}
#endif

#endif
