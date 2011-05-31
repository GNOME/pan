/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/*
 * Pan - A Newsreader for Gtk+
 * Copyright (C) 2002-2006  Charles Kerr <charles@rebelbase.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <config.h>
extern "C" {
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
}
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/data/data.h>
#include "e-charset-combo-box.h"
#include "group-prefs-dialog.h"
#include "hig.h"
#include "pad.h"
#include "pan-file-entry.h"
#include "gtk_compat.h"

using namespace pan;

namespace
{
  void delete_dialog (gpointer castme)
  {
    delete static_cast<GroupPrefsDialog*>(castme);
  }
}

void
GroupPrefsDialog :: save_from_gui ()
{
  // charset...
  const char * tmp = e_charset_combo_box_get_charset (E_CHARSET_COMBO_BOX(_charset));
  _group_prefs.set_string (_group, "character-encoding", tmp);

  // posting profile...
  std::string profile;
  if (gtk_widget_get_sensitive (_profile)) {
    char * pch (gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(_profile)));
    _group_prefs.set_string (_group, "posting-profile", pch);
    g_free (pch);
  }

  // save path...
  const char * pch (file_entry_get (_save_path));
  _group_prefs.set_string (_group, "default-group-save-path", pch);
}

void
GroupPrefsDialog :: response_cb (GtkDialog  * dialog,
                                 int          ,
                                 gpointer     user_data)
{
  static_cast<GroupPrefsDialog*>(user_data)->save_from_gui ();
  gtk_widget_destroy (GTK_WIDGET(dialog));
}

namespace
{
  GtkWidget*
  create_profiles_combo_box (const Data       & data,
                             const Quark      & group,
                             const GroupPrefs & group_prefs)
  {
    GtkWidget * w = gtk_combo_box_text_new ();
    GtkComboBoxText * combo (GTK_COMBO_BOX_TEXT (w));

    typedef std::set<std::string> unique_strings_t;
    const unique_strings_t profiles (data.get_profile_names ());

    if (profiles.empty())
    {
      gtk_combo_box_text_append_text (combo, _("No Profiles defined in Edit|Posting Profiles."));
      gtk_combo_box_set_active (GTK_COMBO_BOX(combo), 0);
      gtk_widget_set_sensitive (w, false);
    }
    else
    {
      const std::string s (group_prefs.get_string (group, "posting-profile", ""));
      int i(0), sel_index (0);
      foreach_const (unique_strings_t, profiles, it) {
        if (*it == s)
          sel_index = i;
        ++i;
        gtk_combo_box_text_append_text (combo, it->c_str());
      }
      gtk_combo_box_set_active (GTK_COMBO_BOX(combo), sel_index);
    }

    return w;
  }
}

GroupPrefsDialog :: GroupPrefsDialog (Data         & data,
                                      const Quark  & group,
                                      GroupPrefs   & group_prefs,
                                      GtkWindow    * parent_window):
  _group (group),
  _group_prefs (group_prefs)
{
  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Group Preferences"),
                                                    parent_window,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
	                                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                                    NULL);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-group-dialog");
  g_signal_connect (dialog, "response", G_CALLBACK(response_cb), this);
  g_signal_connect_swapped (dialog, "destroy", G_CALLBACK(delete_dialog), this);


  int row (0);
  GtkWidget *t, *w, *l;
  t = HIG :: workarea_create ();
  char buf[512];
  g_snprintf (buf, sizeof(buf), _("Properties for %s"), group.c_str());
  HIG::workarea_add_section_title (t, &row, buf);
    HIG :: workarea_add_section_spacer (t, row, 3);
    _charset = w = e_charset_combo_box_new( );
    e_charset_combo_box_set_charset( E_CHARSET_COMBO_BOX(_charset), _group_prefs.get_string (group, "character-encoding", "UTF-8").c_str());
    HIG :: workarea_add_row (t, &row, _("Character _encoding:"), w);
    w = _save_path = file_entry_new (_("Directory for Saving Attachments"));
    char * pch = g_build_filename (g_get_home_dir(), "News", NULL);
    const std::string dir (_group_prefs.get_string (_group, "default-group-save-path", pch));
    g_free (pch);
    file_entry_set (w, dir.c_str());
    HIG :: workarea_add_row (t, &row, _("Directory for _saving attachments:"), w);
    w = _profile = create_profiles_combo_box (data, group, group_prefs);
    l = HIG :: workarea_add_row (t, &row, _("Posting _profile:"), w);
    gtk_widget_set_sensitive (l, gtk_widget_get_sensitive(w));

  gtk_widget_show_all (t);
  gtk_box_pack_start ( GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG( dialog))), t, true, true, 0);
  _root = dialog;
}
