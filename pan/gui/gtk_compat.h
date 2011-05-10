//========================================================================
/**@file	pan/pan/gui/gtk_compat.h
 * @author	kid
 * @date
 * 	Created:	Mon 09 May 2011 04:42:46 PM MDT \n
 * 	Last Update:	Mon 09 May 2011 04:42:46 PM MDT
 */
/*------------------------------------------------------------------------
 * Description:	«description»
 * 
 *========================================================================
 */

#ifndef PAN_GTK_COMPAT_H
#define PAN_GTK_COMPAT_H

namespace {
#if !GTK_CHECK_VERSION(2,24,0)
#define GTK_COMBO_BOX_TEXT(cb) GTK_COMBO_BOX(cb)
  typedef GtkComboBox GtkComboBoxText;
  GtkWidget* gtk_combo_box_text_new()
  {
    return gtk_combo_box_new_text();
  }
  GtkWidget* gtk_combo_box_text_new_with_entry()
  {
    return gtk_combo_box_entry_new_text();
  }
  void gtk_combo_box_text_append_text(GtkComboBoxText *cb, const gchar *t)
  {
    gtk_combo_box_append_text(cb,t);
  }
  gchar *gtk_combo_box_text_get_active_text(GtkComboBoxText *cb)
  {
    return gtk_combo_box_get_active_text(cb);
  }
  void gtk_combo_box_text_remove(GtkComboBoxText *cb, int p)
  {
    gtk_combo_box_remove_text(cb, p);
  }
#endif
}
  
#endif
