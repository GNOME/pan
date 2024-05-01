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

  #define GTK_OBJECT(w) w
  typedef GtkWidget GtkObject;

#ifdef __cplusplus
}
#endif

#endif
