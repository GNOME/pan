#ifndef E_CTE_DIALOG_H
#define E_CTE_DIALOG_H

#include <config.h>
#include <gmime/gmime.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <glib/gi18n.h>

G_BEGIN_DECLS

GMimeContentEncoding e_cte_dialog (const char *title, const char *prompt, GMimeContentEncoding now, GtkWindow *parent);

G_END_DECLS

#endif
