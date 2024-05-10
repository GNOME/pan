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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "group-prefs-dialog.h"

#include <config.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#ifdef HAVE_GTKSPELL
#include <enchant.h>
#endif
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/data/data.h>
#include "e-charset-combo-box.h"
#include "hig.h"
#include "pad.h"
#include "pan-file-entry.h"
#include "pan-colors.h"

#include <iostream>


namespace pan {

namespace
{

  struct Langs
  {
    GList* langs;
  };

  static void
  dict_describe_cb(const char * const lang_tag,
		 const char * const provider_name,
		 const char * const provider_desc,
		 const char * const provider_file,
		 void * user_data)
  {
    Langs *langs = (Langs *)user_data;
    langs->langs = g_list_insert_sorted(langs->langs, g_strdup(lang_tag), (GCompareFunc) strcmp);
  }

#ifdef HAVE_GTKSPELL
  static EnchantBroker *broker = nullptr;
  static GList *langs = nullptr;
  static GtkTextView* view = nullptr;
  Langs l;
#endif
  void init_spell()
  {
#ifdef HAVE_GTKSPELL
    view = GTK_TEXT_VIEW(gtk_text_view_new());
    broker = enchant_broker_init();
    l.langs = langs;
    enchant_broker_list_dicts(broker, dict_describe_cb, &l);
#endif
  }

  void deinit_spell()
  {
#ifdef HAVE_GTKSPELL
    if (view) g_object_ref_sink(view);
    if (broker) enchant_broker_free(broker);
#endif
  }

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
  foreach_const (quarks_v, _groups, it)
    _group_prefs.set_string (*it, "character-encoding", tmp);

  // posting profile...
  std::string profile;
  if (gtk_widget_get_sensitive (_profile)) {
    char * pch (gtk_combo_box_text_get_active_text (GTK_COMBO_BOX_TEXT(_profile)));
    foreach_const (quarks_v, _groups, it)
      _group_prefs.set_string (*it, "posting-profile", pch);
    g_free (pch);
  }

  // save path...
  const char * pch (file_entry_get (_save_path));
  foreach_const (quarks_v, _groups, it)
    _group_prefs.set_string (*it, "default-group-save-path", pch);

  // spellchecker language
#ifdef HAVE_GTKSPELL
  GtkTreeIter iter;
  if (!gtk_combo_box_get_active_iter (GTK_COMBO_BOX(_spellchecker_language), &iter))
		return;

  gchar* name(nullptr);
	GtkTreeModel* model = gtk_combo_box_get_model (GTK_COMBO_BOX(_spellchecker_language));
	gtk_tree_model_get (model, &iter, 0, &name, -1);

  if (name)
  {
    foreach_const (quarks_v, _groups, it)
	  _group_prefs.set_string (*it, "spellcheck-language", name);
    g_free(name);
  }
#endif

  _group_prefs.save () ;

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
                             const quarks_v   & groups,
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
      const std::string s (group_prefs.get_string (groups[0], "posting-profile", ""));
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

  GtkWidget*
  create_spellcheck_combo_box ( const Quark      & group,
                                const GroupPrefs & group_prefs)
  {
    GtkWidget * w(nullptr);

#ifdef HAVE_GTKSPELL
    init_spell();
    deinit_spell();

    GtkTreeModel *model;
    GtkListStore * store = gtk_list_store_new (1, G_TYPE_STRING);
    GtkTreeIter iter, storeit;
    bool valid(false);


    std::string lang = group_prefs.get_string(group, "spellcheck-language","");

    while (l.langs)
    {
      gchar* data = (gchar*)l.langs->data;
      if (data)
      {
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter, 0, data, -1);
        if (g_strcmp0 ((const char*)l.langs->data,lang.c_str())==0) { storeit = iter; valid=true; }
      }
      l.langs = l.langs->next;
    }
    model = GTK_TREE_MODEL(store);
    w = gtk_combo_box_new_with_model (model);
    g_object_unref(store);

    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", 0, nullptr);

    if (valid) gtk_combo_box_set_active_iter (GTK_COMBO_BOX(w), &storeit);

    if (l.langs) g_list_free(l.langs);
#endif
    return w;
  }

  void color_set_cb (GtkColorButton* b, gpointer p)
  {
    GroupPrefsDialog* dialog = static_cast<GroupPrefsDialog*>(p);
    GdkRGBA val;
    gtk_color_chooser_get_rgba ((GtkColorChooser*)b, &val);
    {
      foreach_const (quarks_v, dialog->get_groups(), it)
        dialog->get_prefs().set_group_color(*it, val);
    }
  }


  GtkWidget* new_color_button (const Quark& group, Prefs& prefs, GroupPrefs& gprefs, GroupPrefsDialog* dialog, GtkWidget* w)
  {

    const PanColors& colors (PanColors::get());
    const std::string& def_fg (colors.def_fg);
    const std::string& fg (prefs.get_color_str("group-pane-color-fg", def_fg));
    const GdkRGBA& val (gprefs.get_group_color (group, fg));

    GtkWidget * b = gtk_color_button_new_with_rgba(&val);
    g_signal_connect (b, "color-set", G_CALLBACK(color_set_cb), dialog);

    return b;
  }

}


GroupPrefsDialog :: GroupPrefsDialog (Data            & data,
                                      const quarks_v  & groups,
                                      Prefs           & prefs,
                                      GroupPrefs      & group_prefs,
                                      GtkWindow       * parent_window):
  _groups (groups),
  _prefs(prefs),
  _group_prefs (group_prefs)
{

  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Group Preferences"),
                                                    parent_window,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                                                    nullptr);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-group-dialog");
  g_signal_connect (dialog, "response", G_CALLBACK(response_cb), this);
  g_signal_connect_swapped (dialog, "destroy", G_CALLBACK(delete_dialog), this);

  int row (0);
  GtkWidget *t, *w, *l;
  t = HIG :: workarea_create ();

  char buf[512];
  if (groups.size() != 1)
    g_snprintf (buf, sizeof(buf), _("Properties for Groups"));
  else
    g_snprintf (buf, sizeof(buf), _("Properties for %s"), groups[0].c_str());

  HIG::workarea_add_section_title (t, &row, buf);
  HIG :: workarea_add_section_spacer (t, row, 4);
  w = _charset = e_charset_combo_box_new( );

  e_charset_combo_box_set_charset(
    E_CHARSET_COMBO_BOX(w),
    _group_prefs.get_string (groups[0], "character-encoding", "UTF-8").c_str()
  );

  HIG :: workarea_add_row (t, &row, _("Character _encoding:"), w);

  w = _save_path = file_entry_new (_("Directory for Saving Attachments"));
  char * pch = g_build_filename (g_get_home_dir(), "News", nullptr);
  std::string dir (_prefs.get_string ("default-save-attachments-path", pch));
  if (groups.size() == 1)
    dir = _group_prefs.get_string (groups[0], "default-group-save-path", dir);
  g_free (pch);
  file_entry_set (w, dir.c_str());

  HIG :: workarea_add_row (t, &row, _("Directory for _saving attachments:"), w);
  w = _profile = create_profiles_combo_box (data, groups, group_prefs);
  l = HIG :: workarea_add_row (t, &row, _("Posting _profile:"), w);

  gtk_widget_set_sensitive (l, gtk_widget_get_sensitive(w));
#ifdef HAVE_GTKSPELL
  w = _spellchecker_language = create_spellcheck_combo_box ( groups[0], group_prefs);
  HIG :: workarea_add_row (t, &row, _("Spellchecker _language:"), w);
#endif
  w = _group_color = new_color_button (groups[0], _prefs, _group_prefs, this, dialog);
  HIG :: workarea_add_row(t, &row, _("Group color:"), w);

  gtk_box_pack_start ( GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG( dialog))), t, true, true, 0);
  _root = dialog;
  gtk_widget_show_all (t);

}

} //namespace pan
