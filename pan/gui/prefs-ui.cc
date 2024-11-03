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

#include <config.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/general/debug.h>
#include <pan/general/macros.h>
#include <pan/general/file-util.h>
#include "hig.h"
#include <list>
#include "pad.h"
#include "pan-file-entry.h"
#include "pan/gui/load-icon.h"
#include "prefs-ui.h"
#include "tango-colors.h"
#include "url.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include "e-charset-dialog.h"
#include "actions-extern.h"

#ifdef HAVE_GKR
  #define USE_LIBSECRET_DEFAULT true
#else
  #define USE_LIBSECRET_DEFAULT false
#endif

extern "C" {
  #include <sys/stat.h>
}

#include <algorithm>


using namespace pan;

namespace pan
{
  typedef PrefsDialog::CallBackData CallBackData;

  typedef std::map<std::string,GtkAccelKey> keymap_t;

  struct HotkeyData
  {
    GtkWidget* w;
    int* row;
    Prefs* prefs;
    keymap_t keys;
  };

  static HotkeyData hotkey_data;

  static gboolean hotkey_key_press_cb(GtkWidget *dialog, GdkEventKey *event, gpointer user_data)
  {
    gchar *str;
    gint state;

    CallBackData* data = static_cast<CallBackData*>(user_data);

    state = event->state & gtk_accelerator_get_default_mod_mask();

    if (event->keyval == GDK_KEY_Escape)
      return FALSE;	/* close the dialog, don't allow escape when detecting keybindings. */

    str = gtk_accelerator_name(event->keyval, GdkModifierType(state));

    gtk_label_set_text(GTK_LABEL(data->label), str);
    g_free(str);

    return TRUE;
  }


  static void hotkey_dialog_response_cb(GtkWidget *dialog, gint response, gpointer user_data)
  {
    if (response == GTK_RESPONSE_ACCEPT)
    {
      // update hotkey in database
      static_cast<CallBackData*>(user_data)->dialog->update_hotkey(user_data);
    }
    gtk_widget_destroy(dialog);

    // user_data pointer must not be destroyed otherwise user cannot
    // edit twice the same shortcut without getting a core dump. This
    // may be flagged as a memory leak, but it's limited.

    //delete static_cast<CallBackData*>(user_data);
  }
}

void
PrefsDialog :: update_hotkey (gpointer user_data)
{

  CallBackData* data = static_cast<CallBackData*>(user_data);

  guint lkey;
  GdkModifierType lmods;
  GtkAccelKey acc_key;
  gchar const *str = gtk_label_get_text(GTK_LABEL(data->label));
  gtk_accelerator_parse(str, &lkey, &lmods);

  acc_key.accel_key = lkey;
  acc_key.accel_mods = lmods;

  gtk_accel_map_change_entry(data->name.c_str(), lkey, lmods, true);
  hotkey_data.keys[data->name] = acc_key;

  gtk_entry_set_text(GTK_ENTRY(data->entry), str);

}

void
PrefsDialog :: edit_shortkey (gpointer user_data)
{
  CallBackData* data = static_cast<CallBackData*>(user_data);

  GtkWidget *dialog;
  GtkWidget *label;
  gchar *str;

  dialog = gtk_dialog_new_with_buttons(_("Grab Key"), GTK_WINDOW(root()),
      GtkDialogFlags(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      GTK_STOCK_OK, GTK_RESPONSE_ACCEPT, nullptr);

  str = g_strdup_printf(
      _("Press the combination of the keys\nyou want to use for \"%s\"."), data->value.c_str());
  label = gtk_label_new(str);

  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
		      label,
		      FALSE, FALSE,
		      5);

  data->label = gtk_label_new("");

  gtk_box_pack_start (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))),
		      data->label,
		      FALSE, FALSE,
		      5);

  g_signal_connect(dialog, "key-press-event",
            G_CALLBACK(hotkey_key_press_cb), user_data);
  g_signal_connect(dialog, "response", G_CALLBACK(hotkey_dialog_response_cb), user_data);

  gtk_widget_show_all(dialog);

  g_free(str);
}

void
PrefsDialog :: edit_shortkey_cb (GtkMenuItem *mi, gpointer ptr)
{
  static_cast<CallBackData*>(ptr)->dialog->edit_shortkey(ptr);
}

void
PrefsDialog :: populate_popup (GtkEntry *e, GtkMenu *m)
{
  GtkWidget * mi = gtk_menu_item_new();
  gtk_widget_show (mi);
  gtk_menu_shell_prepend (GTK_MENU_SHELL(m), mi);

  mi = gtk_menu_item_new_with_mnemonic (_("Edit Shortcut"));

  g_signal_connect (mi, "activate", G_CALLBACK(edit_shortkey_cb),
                    g_object_get_data(G_OBJECT(e), "data"));
  gtk_widget_show_all (mi);
  gtk_menu_shell_prepend (GTK_MENU_SHELL(m), mi);
}

namespace pan
{

  std::string get_accel_filename () {
    char * tmp = g_build_filename (file::get_pan_home().c_str(), "accels.txt", nullptr);
    std::string ret (tmp);
    g_free (tmp);
    return ret;
  }

  void
  populate_popup_cb (GtkEntry *e, GtkMenu *m, gpointer ptr)
  {
    g_object_set_data (G_OBJECT (e), "data", ptr);
    static_cast<PrefsDialog::CallBackData*>(ptr)->dialog->populate_popup(e, m);
  }

  void hotkey_entry_changed_cb (GtkEntry * e, gpointer gpointer)
  {

    char* name = static_cast<char*>(gpointer);
    char const *value = gtk_entry_get_text(e);

    gtk_entry_set_icon_activatable(e, GTK_ENTRY_ICON_PRIMARY, false);
    // gtk_entry_set_icon_from_stock(e, GTK_ENTRY_ICON_PRIMARY, nullptr);
    gtk_entry_set_icon_tooltip_text (e, GTK_ENTRY_ICON_PRIMARY, nullptr);

    // empty text
    if (!value || !*value)
    {
      gtk_entry_set_icon_from_icon_name(e, GTK_ENTRY_ICON_PRIMARY, nullptr);
      gtk_entry_set_icon_tooltip_text (e, GTK_ENTRY_ICON_PRIMARY, nullptr);
      // reset in map and remove accelerator
      GtkAccelKey tmp;
      guint tmpkey;
      GdkModifierType tmpmod;
      gtk_accelerator_parse ("",&tmpkey,&tmpmod);
      hotkey_data.keys[name] = tmp;
      gtk_accel_map_change_entry (name, tmpkey, tmpmod, true);
      return;
    }

    guint key;
    GdkModifierType mod;
    GtkAccelKey acc_key;

    gtk_accelerator_parse (value,&key,&mod);
    acc_key.accel_key = key;
    acc_key.accel_mods = mod;

    if (!gtk_accelerator_valid(acc_key.accel_key, acc_key.accel_mods))
      {
        gtk_entry_set_icon_from_icon_name(e, GTK_ENTRY_ICON_PRIMARY, "dialog-warning");
        gtk_entry_set_icon_tooltip_text (e, GTK_ENTRY_ICON_PRIMARY, _("Error: Shortcut key is invalid!"));
        return;
      }

    bool found(false);

    // search for duplicate key entry
    foreach (keymap_t, hotkey_data.keys, it)
    {
      if (!strcmp(name, it->first.c_str())) continue;
      if (it->second.accel_key == key && it->second.accel_mods == mod) { found=true; break;}
    }

    if (found)
    {
      gtk_entry_set_icon_from_icon_name(e, GTK_ENTRY_ICON_PRIMARY, "dialog-warning");
      gtk_entry_set_icon_tooltip_text(e, GTK_ENTRY_ICON_PRIMARY, _("Error: Shortcut key already exists!"));
    }
    else
    {
      gtk_entry_set_icon_from_icon_name(e, GTK_ENTRY_ICON_PRIMARY, nullptr);
      gtk_entry_set_icon_tooltip_text(e, GTK_ENTRY_ICON_PRIMARY, nullptr);
      hotkey_data.keys[name] = acc_key;
    }
  }

  void process_accels(gpointer _data,
                      gchar const *accel_path,
                      guint accel_key,
                      GdkModifierType accel_mods,
                      gboolean changed)
  {
    HotkeyData* data = static_cast<HotkeyData*>(_data);
    GtkAccelKey key;
    key.accel_key = accel_key;
    key.accel_mods = accel_mods;
    data->keys[accel_path] = key;

    guint _key;
    GdkModifierType _mod;
    GtkAccelKey acc_key;

  }

  void save_accels()
  {

    // get changed accels from map and reset them to their values
    foreach (keymap_t, hotkey_data.keys, it)
    {
      gtk_accel_map_change_entry (it->first.c_str(),
                                  it->second.accel_key,
                                  it->second.accel_mods,
                                  true);
    }

    // save 'em
    const std::string accel_filename (get_accel_filename());
    gtk_accel_map_save (accel_filename.c_str());
    chmod (accel_filename.c_str(), 0600);
  }

  void delete_prefs_dialog (gpointer castme)
  {
    PrefsDialog* pd(static_cast<PrefsDialog*>(castme));
    save_accels();
    pd->prefs().set_int("prefs-last-selected-page",
                        gtk_notebook_get_current_page(GTK_NOTEBOOK(pd->notebook())));
    pd->prefs().remove_listener(pd);
    delete pd;
  }

  void response_cb (GtkDialog * dialog, int, gpointer)
  {
    gtk_widget_destroy (GTK_WIDGET(dialog));
  }

  #define PREFS_KEY "prefs-key"
  #define PREFS_VAL "prefs-val"

  void toggled_cb (GtkToggleButton * toggle, gpointer prefs_gpointer)
  {
    char const *key =
      (char const *)g_object_get_data(G_OBJECT(toggle), PREFS_KEY);
    if (key)
      static_cast<Prefs*>(prefs_gpointer)->set_flag (key, gtk_toggle_button_get_active(toggle));
  }

  void entry_changed_cb (GtkEntry * e, gpointer prefs_gpointer)
  {
    char const *key = (char const *)g_object_get_data(G_OBJECT(e), PREFS_KEY);
    char const *val = gtk_entry_get_text(GTK_ENTRY(e));
    if (key && val)
      static_cast<Prefs*>(prefs_gpointer)->set_string (key, val);
  }

  void set_string_from_radio_cb (GtkToggleButton * toggle, gpointer prefs_gpointer)
  {
    char const *key =
      (char const *)g_object_get_data(G_OBJECT(toggle), PREFS_KEY);
    char const *val =
      (char const *)g_object_get_data(G_OBJECT(toggle), PREFS_VAL);
    if (key && val && gtk_toggle_button_get_active(toggle))
      static_cast<Prefs*>(prefs_gpointer)->set_string (key, val);
  }

  GtkWidget *new_check_button(char const *mnemonic,
                              char const *key,
                              bool fallback,
                              Prefs &prefs)
  {
    GtkWidget * t = gtk_check_button_new_with_mnemonic (mnemonic);
    g_object_set_data_full (G_OBJECT(t), PREFS_KEY, g_strdup(key), g_free);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(t), prefs.get_flag (key, fallback));
    g_signal_connect (t, "toggled", G_CALLBACK(toggled_cb), &prefs);
    return t;
  }

  GtkWidget *new_entry(char const *key, char const *fallback, Prefs &prefs)
  {
    GtkWidget * t = gtk_entry_new();
    g_object_set_data_full (G_OBJECT(t), PREFS_KEY, g_strdup(key), g_free);
    gtk_entry_set_text (GTK_ENTRY(t), prefs.get_string (key, fallback).str);
    g_signal_connect (t, "changed", G_CALLBACK(entry_changed_cb), &prefs);
    return t;
  }

  GtkWidget *new_hotkey_entry(char const *value, char const *name, gpointer ptr)
  {

    GtkWidget * t = gtk_entry_new();
    static_cast<CallBackData*>(ptr)->entry = t;
    gtk_entry_set_text (GTK_ENTRY(t), value);
    g_signal_connect (t, "changed", G_CALLBACK(hotkey_entry_changed_cb), gpointer(name));
    g_signal_connect (t, "populate-popup", G_CALLBACK(populate_popup_cb), ptr);

    return t;
  }

  GtkWidget *new_layout_radio(GtkWidget *prev,
                              char const *icon_file,
                              char const *value,
                              std::string &cur,
                              Prefs &prefs)
  {
    GtkWidget * r = prev==nullptr
      ? gtk_radio_button_new (nullptr)
      : gtk_radio_button_new_from_widget (GTK_RADIO_BUTTON(prev));
    GdkPixbuf * pixbuf = load_icon (icon_file);
    GtkWidget * image = gtk_image_new_from_pixbuf (pixbuf);
    if (pixbuf != nullptr)
      g_object_unref (pixbuf);
    gtk_container_add (GTK_CONTAINER(r), image);
    g_object_set_data_full (G_OBJECT(r), PREFS_KEY, g_strdup("pane-layout"), g_free);
    g_object_set_data_full (G_OBJECT(r), PREFS_VAL, g_strdup(value), g_free);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(r), cur==value);
    g_signal_connect (r, "toggled", G_CALLBACK(set_string_from_radio_cb), &prefs);
    return r;
  }

  void spin_value_changed_cb( GtkSpinButton *spin, gpointer data)
  {
    char const *key =
      (char const *)g_object_get_data(G_OBJECT(spin), PREFS_KEY);
    Prefs *prefs = static_cast<Prefs*>(data);
    prefs->set_int(key, gtk_spin_button_get_value_as_int(spin));
  }

  GtkWidget *new_spin_button(char const *key, int low, int high, Prefs &prefs)
  {
    guint tm = prefs.get_int(key, low );
    GtkAdjustment *adj = (GtkAdjustment*) gtk_adjustment_new(tm, low, high, 1.0, 1.0, 0.0);
    GtkWidget *w = gtk_spin_button_new( adj, 1.0, 0);
    g_object_set_data_full(G_OBJECT(w), PREFS_KEY, g_strdup(key), g_free);
    g_signal_connect (w, "value_changed", G_CALLBACK(spin_value_changed_cb), &prefs);
    return w;
  }

  GtkWidget *new_orient_radio(GtkWidget *prev,
                              char const *label,
                              char const *value,
                              std::string &cur,
                              Prefs &prefs)
  {
    GtkWidget * r = prev==nullptr
      ? gtk_radio_button_new_with_label (nullptr, label)
      : gtk_radio_button_new_with_label_from_widget (GTK_RADIO_BUTTON(prev), label);
    g_object_set_data_full (G_OBJECT(r), PREFS_KEY, g_strdup("pane-orient"), g_free);
    g_object_set_data_full (G_OBJECT(r), PREFS_VAL, g_strdup(value), g_free);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(r), cur==value);
    g_signal_connect (r, "toggled", G_CALLBACK(set_string_from_radio_cb), &prefs);
    return r;
  }

  GtkWidget *new_label_with_icon(char const *mnemonic,
                                 char const *label,
                                 char const *icon_file,
                                 Prefs &prefs)
  {
    std::string what = prefs.get_string("elements-show-tabs", "text");
    bool const text = "text" == what;
    bool const icons = "icons" == what;
    bool const both = "both" == what;

    GtkWidget* hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);
    GdkPixbuf * pixbuf = load_icon (icon_file);
    GtkWidget * image = gtk_image_new_from_pixbuf (pixbuf);
    if (pixbuf != nullptr)
      g_object_unref(pixbuf);
    if (icon_file && (icons || both)) gtk_box_pack_start (GTK_BOX(hbox), image, true, true, 0);
    if (text || both) gtk_box_pack_start (GTK_BOX(hbox), gtk_label_new_with_mnemonic(mnemonic), true, true, 0);
    gtk_widget_set_tooltip_text (hbox, label);
    gtk_widget_show_all(hbox);
    return hbox;
  }

  std::string remove_underscores (std::string& src)
  {
    std::string res (src);
    res.erase(std::remove(res.begin(), res.end(), '_'), res.end());
    return res;
  }

  std::list<CallBackData*> fill_pref_hotkeys(GtkWidget* t, int& row, Prefs& prefs, gpointer dialog_ptr)
  {

    HIG::workarea_add_section_spacer (t, row, hotkey_data.keys.size());

    GtkWidget* w, *l;
    gchar* keyval;
    std::list<CallBackData*> ptr_list;

    foreach (keymap_t, hotkey_data.keys, it)
    {
      keyval = gtk_accelerator_name (it->second.accel_key, it->second.accel_mods);

      std::string stripped = it->first;
      size_t f = stripped.find_last_of("/");
      stripped = f != std::string::npos ? stripped.substr(f+1,stripped.size()) : stripped;

      CallBackData* data = new CallBackData();
      // store the pointer so it can be destroyed when preferences widget is destroyed
      ptr_list.push_back(data);

      data->dialog = (PrefsDialog*)dialog_ptr;
      if (!it->first.empty()) data->name = it->first;
      data->value = stripped;

      w = new_hotkey_entry(keyval, it->first.c_str(), data);
      std::string label = remove_underscores(action_trans[stripped]);
      l = gtk_label_new(label.c_str());
      HIG :: workarea_add_row (t, &row, w, l);
    }

    return ptr_list;
  }

  void set_prefs_string_from_editable (GtkEditable * editable, gpointer prefs_gpointer)
  {
    Prefs * prefs (static_cast<Prefs*>(prefs_gpointer));
    char const *key =
      (char const *)g_object_get_data(G_OBJECT(editable), PREFS_KEY);
    char * val = gtk_editable_get_chars (editable, 0, -1);
    prefs->set_string (key, val);
    g_free (val);
  }

  void maybe_make_widget_visible (GtkComboBox * c, gpointer user_data)
  {
    GtkWidget * w (GTK_WIDGET(user_data));
    GtkWidget * c_parent (gtk_widget_get_parent (GTK_WIDGET(c)));
    GtkWidget * w_parent (gtk_widget_get_parent (GTK_WIDGET(w)));
    GtkTreeModel * model = gtk_combo_box_get_model (c);
    int const n_rows(gtk_tree_model_iter_n_children(model, nullptr));
    bool const do_show(gtk_combo_box_get_active(c) == (n_rows - 1));

    if (do_show && !w_parent && c_parent) // add it
    {
      gtk_box_pack_start (GTK_BOX(c_parent), w, true, true, 0);
      gtk_widget_show (w);
      g_object_unref (G_OBJECT(w));
    }
    else if (!do_show && w_parent) // remove it
    {
      g_object_ref (G_OBJECT(w));
      gtk_container_remove (GTK_CONTAINER(w_parent), w);
    }
    else if (!do_show && !w_parent && !c_parent)
    {
      gtk_widget_show_all (GTK_WIDGET(c));
    }
  }

  void set_prefs_string_from_combo_box_entry (GtkComboBoxText * c, gpointer user_data)
  {
    char const *key = (char const *)g_object_get_data(G_OBJECT(c), PREFS_KEY);
    char * val = gtk_combo_box_text_get_active_text (c);
    static_cast<Prefs*>(user_data)->set_string (key, val);
    g_free (val);
  }

  GtkWidget* html_previewer_new (Prefs& prefs)
  {
    char const *key = "html-previewer";
    //    const std::string editor = prefs.get_string (key, "mutt");
    //    editors.insert (editor);
    GtkWidget * c = gtk_combo_box_text_new_with_entry ();
    g_object_set_data_full (G_OBJECT(c), PREFS_KEY, g_strdup(key), g_free);
//    foreach_const (std::set<std::string>, editors, it)
//      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(c), it->c_str());
//    gtk_combo_box_set_active (GTK_COMBO_BOX(c),
//                              (int)std::distance (editors.begin(), editors.find(editor)));
    g_signal_connect (c, "changed", G_CALLBACK(set_prefs_string_from_combo_box_entry), &prefs);
    return c;
  }

  GtkWidget* editor_new (Prefs& prefs)
  {
    std::set<std::string> editors;
    URL :: get_default_editors (editors);
    char const *key = "editor";
    const std::string editor = prefs.get_string (key, *editors.begin());
    editors.insert (editor);
    GtkWidget * c = gtk_combo_box_text_new_with_entry ();
    g_object_set_data_full (G_OBJECT(c), PREFS_KEY, g_strdup(key), g_free);
    foreach_const (std::set<std::string>, editors, it)
      gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT(c), it->c_str());
    gtk_combo_box_set_active (GTK_COMBO_BOX(c),
                              (int)std::distance (editors.begin(), editors.find(editor)));
    g_signal_connect (c, "changed", G_CALLBACK(set_prefs_string_from_combo_box_entry), &prefs);
    return c;
  }

  void set_prefs_string_from_combobox (GtkComboBox * c, gpointer user_data)
  {
    Prefs * prefs (static_cast<Prefs*>(user_data));
    char const *key = (char const *)g_object_get_data(G_OBJECT(c), PREFS_KEY);

    prefs->_rules_changed = strcmp(key,"rules-");

    int const column =
      GPOINTER_TO_INT(g_object_get_data(G_OBJECT(c), "column"));
    int const row(gtk_combo_box_get_active(c));
    GtkTreeModel * m = gtk_combo_box_get_model (c);
    GtkTreeIter i;
    if (gtk_tree_model_iter_nth_child (m, &i, nullptr, row)) {
      char * val (nullptr);
      gtk_tree_model_get (m, &i, column, &val, -1);
      prefs->set_string (key, val);
      g_free (val);
    }
  }

  GtkWidget *new_tabs_combo_box(Prefs &prefs, char const *mode_key)
  {

    char const *strings[3][2] = {
      {N_("Show only icons"), "icons"},
      {N_("Show only text"), "text"},
      {N_("Show icons and text"), "both"},
    };

    const std::string mode (prefs.get_string (mode_key, "text"));
    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

    int sel_index (0);
    for (size_t i=0; i<G_N_ELEMENTS(strings); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, gettext(strings[i][0]), 1, strings[i][1], -1);
      if (mode == strings[i][1])
        sel_index = i;
    }

    GtkWidget * c = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (c), renderer, true);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (c), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(c), sel_index);
    g_object_set_data_full (G_OBJECT(c), PREFS_KEY, g_strdup(mode_key), g_free);
    g_object_set_data (G_OBJECT(c), "column", GINT_TO_POINTER(1));
    g_signal_connect (c, "changed", G_CALLBACK(set_prefs_string_from_combobox), &prefs);

    gtk_widget_show_all(c);

    return c;
  }

  GtkWidget *url_handler_new(Prefs &prefs,
                             char const *mode_key,
                             char const *mode_fallback,
                             char const *custom_key,
                             char const *custom_fallback,
                             GtkWidget *&setme_mnemonic_target)
  {
    // build the combo box...
    const std::string mode (prefs.get_string (mode_key, mode_fallback));
    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
    char const *strings[5][2] = {{N_("Use GNOME Preferences"), "gnome"},
                                 {N_("Use KDE Preferences"), "kde"},
                                 {N_("Use OS X Preferences"), "mac"},
                                 {N_("Use Windows Preferences"), "windows"},
                                 {N_("Custom Command:"), "custom"}};
    int sel_index (0);
    for (size_t i=0; i<G_N_ELEMENTS(strings); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, _(strings[i][0]), 1, strings[i][1], -1);
      if (mode == strings[i][1])
        sel_index = i;
    }

    GtkWidget * c = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (c), renderer, true);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (c), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(c), sel_index);
    g_object_set_data_full (G_OBJECT(c), PREFS_KEY, g_strdup(mode_key), g_free);
    g_object_set_data (G_OBJECT(c), "column", GINT_TO_POINTER(1));
    g_signal_connect (c, "changed", G_CALLBACK(set_prefs_string_from_combobox), &prefs);

    // build the custom entry...
    GtkWidget * e = gtk_entry_new ();
    const std::string custom (prefs.get_string (custom_key, custom_fallback));
    gtk_entry_set_text (GTK_ENTRY(e), custom.c_str());
    g_object_set_data_full (G_OBJECT(e), PREFS_KEY, g_strdup(custom_key), g_free);
    g_signal_connect (e, "changed", G_CALLBACK(set_prefs_string_from_editable), &prefs);

    // tie them together...
    g_signal_connect (c, "changed", G_CALLBACK(maybe_make_widget_visible), e);
    GtkWidget * h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    gtk_box_pack_start (GTK_BOX(h), c, true, true, 0);
    gtk_box_pack_start (GTK_BOX(h), e, true, true, 0);

    maybe_make_widget_visible (GTK_COMBO_BOX(c), e);
    setme_mnemonic_target = c;
    return h;
  }

/*
      Scores:
    "Scores of 9999 or more:"
    "Scores from 5000 to 9998:"
    "Scores from 1 to 4999:"
    "Scores from -9998 to -1:"
*/
  GtkWidget *score_handler_new(Prefs &prefs,
                               char const *mode_key,
                               char const *mode_fallback,
                               GtkWidget *&setme_mnemonic_target)
  {
    // build the combo box...
    const std::string mode (prefs.get_string (mode_key, mode_fallback));
    GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);
    char const *strings[7][2] = {{N_("Disabled"), "never"},
                                 {N_("Only new (score == 0)"), "new"},
                                 {N_("9999 or more"), "watched"},
                                 {N_("5000 to 9998"), "high"},
                                 {N_("1 to 4999"), "medium"},
                                 {N_("-9998 to -1"), "low"},
                                 {N_("-9999 or less"), "ignored"}};
    int sel_index (0);
    for (size_t i=0; i<G_N_ELEMENTS(strings); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, _(strings[i][0]), 1, strings[i][1], -1);
      if (mode == strings[i][1])
        sel_index = i;
    }

    GtkWidget * c = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (c), renderer, true);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (c), renderer, "text", 0, nullptr);
    gtk_combo_box_set_active (GTK_COMBO_BOX(c), sel_index);
    g_object_set_data_full (G_OBJECT(c), PREFS_KEY, g_strdup(mode_key), g_free);
    g_object_set_data (G_OBJECT(c), "column", GINT_TO_POINTER(1));
    g_signal_connect (c, "changed", G_CALLBACK(set_prefs_string_from_combobox), &prefs);

    setme_mnemonic_target = c;
    return c;
  }

  void font_set_cb (GtkFontButton* b, gpointer prefs_gpointer)
  {
    char const *key = (char const *)g_object_get_data(G_OBJECT(b), PREFS_KEY);
    char const *val = gtk_font_chooser_get_font((GtkFontChooser*)b);
    if (key && *key && val && *val)
      static_cast<Prefs*>(prefs_gpointer)->set_string (key, val);
  }

  GtkWidget *new_font_button(char const *key,
                             char const *fallback,
                             Prefs &prefs)
  {
    const std::string val (prefs.get_string (key, fallback));
    GtkWidget * b = gtk_font_button_new_with_font (val.c_str());
    g_object_set_data_full (G_OBJECT(b), PREFS_KEY, g_strdup(key), g_free);
    g_signal_connect (b, "font-set", G_CALLBACK(font_set_cb), &prefs);
    return b;
  }

  void color_set_cb (GtkColorButton* b, gpointer prefs_gpointer)
  {
    char const *key = (char const *)g_object_get_data(G_OBJECT(b), PREFS_KEY);
    GdkRGBA val;
    gtk_color_chooser_get_rgba((GtkColorChooser*)b, &val);
    if (key && *key)
      static_cast<Prefs*>(prefs_gpointer)->set_color (key, val);
  }

  GtkWidget *new_color_button(char const *key,
                              char const *fallback,
                              Prefs &prefs)
  {
    GdkRGBA rgba_val (prefs.get_color (key, fallback));;
    GtkWidget * b = gtk_color_button_new_with_rgba (&rgba_val);
    g_object_set_data_full (G_OBJECT(b), PREFS_KEY, g_strdup(key), g_free);
    g_signal_connect (b, "color-set", G_CALLBACK(color_set_cb), &prefs);
    return b;
  }
}

void PrefsDialog ::update_default_charset_label(StringView const &value)
{
  char buf[256];
  g_snprintf(buf, sizeof(buf),_("Select default <u>global</u> character set. Current setting: <b>%s</b>."),
             value.str);
  gtk_label_set_markup(GTK_LABEL(charset_label), buf);
  gtk_widget_show_all(charset_label);
}

void PrefsDialog ::on_prefs_string_changed(StringView const &key,
                                           StringView const &value)
{

  if (key == "default-charset")
  {
    _prefs.save();
    update_default_charset_label(value);
  }
}

void PrefsDialog ::on_prefs_flag_changed(StringView const &key, bool value)
{

  if (key == "allow-multiple-instances")
    _prefs.save();
}

namespace
{
  void select_prefs_charset_cb (GtkButton *, gpointer user_data)
  {
    PrefsDialog* pd (static_cast<PrefsDialog*>(user_data));
    std::string def = pd->prefs().get_string("default-charset", "UTF-8");
    char * tmp = e_charset_dialog (_("Character Encoding"),
                               _("Global Character Set Settings"),
                               def.c_str(), GTK_WINDOW(pd->root()));

    if (!tmp) return;
    pd->prefs().set_string("default-charset", tmp);
  }

}

namespace
{
  struct HeaderColInfo
  {
    GtkTreeView * view;
    GtkTreeSelection * sel;
    GtkListStore * store;
    Prefs * prefs;
  };

  std::string get_header_column_string (GtkTreeModel * model)
  {
    std::string s;
    GtkTreeIter iter;
    if (gtk_tree_model_get_iter_first (model, &iter)) do {
      gboolean enabled;
      char * key (nullptr);
      gtk_tree_model_get (model, &iter, 0, &enabled, 1, &key, -1);
      if (enabled)
        s += std::string(key) + ",";
      g_free (key);
    } while (gtk_tree_model_iter_next (model, &iter));
    if (!s.empty())
      s.resize (s.size()-1); // strip trailing comma
    return s;
  }

  void header_column_up_cb (GtkButton *, gpointer user_data)
  {
    HeaderColInfo& info (*static_cast<HeaderColInfo*>(user_data));
    GtkTreeIter sel_iter;
    gtk_tree_selection_get_selected (info.sel, nullptr, &sel_iter);
    GtkTreePath * path = gtk_tree_model_get_path (GTK_TREE_MODEL(info.store), &sel_iter);
    if (gtk_tree_path_prev (path)) {
      GtkTreeIter prev_iter;
      gtk_tree_model_get_iter (GTK_TREE_MODEL(info.store), &prev_iter, path);
      gtk_list_store_move_after (info.store, &prev_iter, &sel_iter);
      info.prefs->set_string ("header-pane-columns", get_header_column_string (GTK_TREE_MODEL(info.store)));
    }
    gtk_tree_path_free (path);
  }

  void header_column_down_cb (GtkButton *, gpointer user_data)
  {
    HeaderColInfo& info (*static_cast<HeaderColInfo*>(user_data));
    GtkTreeIter sel_iter;
    gtk_tree_selection_get_selected (info.sel, nullptr, &sel_iter);
    GtkTreeIter next_iter = sel_iter;
    if (gtk_tree_model_iter_next (GTK_TREE_MODEL(info.store), &next_iter)) {
      gtk_list_store_move_after (info.store, &sel_iter, &next_iter);
      info.prefs->set_string ("header-pane-columns", get_header_column_string (GTK_TREE_MODEL(info.store)));
    }
  }

  void header_col_enabled_toggled_cb (GtkCellRendererToggle * ,
	                              gchar                 * path_str,
	                              gpointer                user_data)
  {
    HeaderColInfo& info (*static_cast<HeaderColInfo*>(user_data));
    GtkTreeModel * model = GTK_TREE_MODEL(info.store);
    GtkTreeIter iter;
    GtkTreePath *path = gtk_tree_path_new_from_string (path_str);

    // toggle the value
    gboolean fixed;
    gtk_tree_model_get_iter (model, &iter, path);
    gtk_tree_model_get (model, &iter, 0, &fixed, -1);
    fixed = !fixed;
    gtk_list_store_set (info.store, &iter, 0, fixed, -1);
    info.prefs->set_string ("header-pane-columns", get_header_column_string (GTK_TREE_MODEL(info.store)));

    // clean up
    gtk_tree_path_free (path);
  }

  GtkWidget* header_columns_layout_new (Prefs& prefs)
  {
    typedef std::map<std::string,std::string> key_to_name_t;
    key_to_name_t key_to_name;
    key_to_name["action"] = _("Action");
    key_to_name["author"] = _("Author");
    key_to_name["bytes"] = _("Bytes");
    key_to_name["date"] = _("Date");
    key_to_name["lines"] = _("Lines");
    key_to_name["score"] = _("Score");
    key_to_name["state"] = _("State");
    key_to_name["subject"] = _("Subject");

    GtkListStore * store = gtk_list_store_new (3, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING);
    const std::string column_keys = prefs.get_string ("header-pane-columns", "state,action,subject,score,author,lines,date");
    StringView v(column_keys), tok;
    while (v.pop_token (tok, ',')) {
      const std::string key (tok.to_string());
      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, true,
                                        1, key.c_str(),
                                        2, key_to_name[key].c_str(), -1);
      key_to_name.erase (key);
    }
    foreach_const (key_to_name_t, key_to_name, it) {
      GtkTreeIter iter;
      gtk_list_store_append (store, &iter);
      gtk_list_store_set (store, &iter, 0, false,
                                        1, it->first.c_str(),
                                        2, it->second.c_str(), -1);
    }

    GtkWidget * view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(store));
    GtkTreeSelection * sel = gtk_tree_view_get_selection (GTK_TREE_VIEW(view));

    HeaderColInfo * info = g_new (HeaderColInfo, 1);
    info->store = store;
    info->prefs = &prefs;
    info->view = GTK_TREE_VIEW(view);
    info->sel = sel;
    g_object_weak_ref (G_OBJECT(view), (GWeakNotify)g_free, info);

    GtkCellRenderer * r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TOGGLE, nullptr));
    GtkTreeViewColumn * col = gtk_tree_view_column_new_with_attributes (_("Enabled"), r, "active", 0, nullptr);
    gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);
    g_signal_connect (r, "toggled", G_CALLBACK(header_col_enabled_toggled_cb), info);
    r = GTK_CELL_RENDERER (g_object_new (GTK_TYPE_CELL_RENDERER_TEXT, nullptr));
    col = gtk_tree_view_column_new_with_attributes (_("Column Name"), r, "text", 2, nullptr);
    gtk_tree_view_append_column (GTK_TREE_VIEW(view), col);
    gtk_tree_selection_set_mode (sel, GTK_SELECTION_BROWSE);
    GtkTreeIter iter;
    gtk_tree_model_get_iter_first (GTK_TREE_MODEL(store), &iter);
    gtk_tree_selection_select_iter (sel, &iter);

    GtkWidget * f = gtk_frame_new (nullptr);
    gtk_frame_set_shadow_type (GTK_FRAME(f), GTK_SHADOW_IN);
    gtk_container_add (GTK_CONTAINER(f), view);
    GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    gtk_box_pack_start (GTK_BOX(hbox), f, true, true, 0);
    GtkWidget * vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, PAD);
    GtkWidget * up = gtk_button_new_from_stock (GTK_STOCK_GO_UP);
    gtk_box_pack_start (GTK_BOX(vbox), up, false, false, 0);
    GtkWidget * down = gtk_button_new_from_stock (GTK_STOCK_GO_DOWN);
    gtk_box_pack_start (GTK_BOX(vbox), down, false, false, 0);
    gtk_box_pack_start (GTK_BOX(hbox), vbox, false, false, 0);
    g_signal_connect (up, "clicked", G_CALLBACK(header_column_up_cb), info);
    g_signal_connect (down, "clicked", G_CALLBACK(header_column_down_cb), info);

    return hbox;
  }

  void font_toggled_cb (GtkToggleButton * tb, gpointer user_data)
  {
    bool const active(gtk_toggle_button_get_active(tb));
    gtk_widget_set_sensitive (GTK_WIDGET(user_data), active);
  }
}

PrefsDialog :: ~PrefsDialog ()
{
  // free pointer created when constructing shortcut editor widget
  while(!shortcut_ptr_list.empty()) {
    free(shortcut_ptr_list.back());
    shortcut_ptr_list.pop_back();
  }
}


PrefsDialog :: PrefsDialog (Prefs& prefs, GtkWindow* parent):
  _prefs (prefs)
{
  prefs.add_listener(this);

  gtk_accel_map_foreach (&hotkey_data, process_accels);

  GtkWidget * dialog = gtk_dialog_new_with_buttons (_("Pan: Preferences"), parent,
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
                                                    nullptr);
  gtk_window_set_role (GTK_WINDOW(dialog), "pan-preferences-dialog");
  g_signal_connect (dialog, "response", G_CALLBACK(response_cb), this);
  g_signal_connect_swapped (dialog, "destroy", G_CALLBACK(delete_prefs_dialog), this);

  GtkWidget* notebook = _notebook = gtk_notebook_new ();
  gtk_notebook_set_scrollable (GTK_NOTEBOOK(notebook), true);

  // Behavior
  int row (0);
  GtkWidget *h, *w, *l, *b, *t;
  t = HIG :: workarea_create ();
  HIG::workarea_add_section_title (t, &row, _("Mouse"));
    HIG :: workarea_add_section_spacer (t, row, 2);
    w = new_check_button (_("Single-click activates, rather than selects, _groups"), "single-click-activates-group", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Single-click activates, rather than selects, _articles"), "single-click-activates-article", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
  HIG::workarea_add_section_divider (t, &row);
  HIG::workarea_add_section_title (t, &row, _("Groups"));
    HIG::workarea_add_section_spacer (t, row, 5);
    w = new_check_button (_("Get new headers in subscribed groups on _startup"), "get-new-headers-on-startup", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Get new headers when _entering group"), "get-new-headers-when-entering-group", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Mark entire group _read when leaving group"), "mark-group-read-when-leaving-group", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Mark entire group read before getting _new headers"), "mark-group-read-before-xover", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("E_xpand all threads when entering group"), "expand-threads-when-entering-group", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);

    HIG::workarea_add_section_divider (t, &row);

    HIG :: workarea_add_section_title (t, &row, _("Articles"));
    HIG :: workarea_add_section_spacer (t, row, 6);
    w = new_check_button (_("Mark downloaded articles read"), "mark-downloaded-articles-read", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Space selects next article rather than next unread"), "space-selects-next-article", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Expand threads upon selection"), "expand-selected-articles", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Always ask before deleting an article"), "show-deletion-confirm-dialog", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Smooth scrolling"), "smooth-scrolling", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);

    HIG::workarea_add_section_divider (t, &row);

    HIG :: workarea_add_section_title (t, &row, _("Article Cache"));
    w = new_check_button (_("Clear article cache on shutdown"), "clear-article-cache-on-shutdown", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_spin_button ("cache-size-megs", 10, 1024*16, prefs);
    l = gtk_label_new(_("Size of article cache (in MiB):"));
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    HIG::workarea_add_row (t, &row, l, w);
    w = new_entry ("cache-file-extension", "msg", prefs);
    l = gtk_label_new(_("File extension for cached articles: "));
    HIG :: workarea_add_row (t, &row, l, w);

    HIG::workarea_add_section_divider (t, &row);
    HIG :: workarea_add_section_title (t, &row, _("Tabs"));
    w = new_tabs_combo_box(prefs, "elements-show-tabs");
    HIG :: workarea_add_wide_control (t, &row, w);

  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Behavior"), _("Behavior"), "icon_prefs_behavior.png", prefs));

  // pane options
  row = 0;
  t = HIG :: workarea_create ();
    HIG::workarea_add_section_spacer (t, row, 1);
    HIG :: workarea_add_section_title (t, &row, _("Task Pane"));
    w = new_check_button (_("Show task pane popups"), "show-taskpane-popups", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_check_button (_("Show Download Meter"), "dl-meter-show", true, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    HIG::workarea_add_section_divider (t, &row);
  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Panes"), _("Panes"), "icon_prefs_panes.png", prefs));

  //charset
  row = 0;
  t = HIG :: workarea_create ();
    HIG :: workarea_add_section_spacer (t, row, 1);
    HIG :: workarea_add_section_title (t, &row, _("Language Settings"));
    w = gtk_button_new_with_label (_("Font"));
    l = charset_label = gtk_label_new (nullptr);
    update_default_charset_label(_prefs.get_string("default-charset","UTF-8"));
    g_signal_connect (w, "clicked", G_CALLBACK(select_prefs_charset_cb), this);
    HIG::workarea_add_row (t, &row, w, l);

    // systray and notify popup
#ifdef HAVE_LIBNOTIFY
    HIG :: workarea_add_section_title (t, &row, _("System Tray Behavior"));
    HIG :: workarea_add_section_spacer (t, row, 3);
    w = new_check_button (_("Show notifications"), "use-notify", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
#endif

    // allow multiple instances (seperate, not communicating with dbus)
    HIG :: workarea_add_section_title (t, &row, _("Startup Behavior"));
    HIG :: workarea_add_section_spacer (t, row, 1);
    w = new_check_button (_("Allow multiple instances of Pan"), "allow-multiple-instances", false, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);

    // Autosave Features
    HIG :: workarea_add_section_spacer (t, row, 2);
    HIG :: workarea_add_section_title (t, &row, _("Autosave Article Draft"));
    w = new_spin_button ("draft-autosave-timeout-min", 0, 60, prefs);
    l = gtk_label_new(_("Minutes to autosave the current Article Draft: "));
    gtk_label_set_xalign (GTK_LABEL(l), 0.0);
    gtk_label_set_yalign (GTK_LABEL(l), 0.5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    HIG::workarea_add_row (t, &row, l, w);
    HIG::workarea_add_section_divider (t, &row);
    HIG :: workarea_add_section_title (t, &row, _("Autosave Articles"));
    w = new_spin_button ("newsrc-autosave-timeout-min", 0, 60, prefs);
    l = gtk_label_new(_("Minutes to autosave newsrc files: "));
    gtk_label_set_xalign (GTK_LABEL(l), 0.0);
    gtk_label_set_yalign (GTK_LABEL(l), 0.5);
    gtk_label_set_mnemonic_widget(GTK_LABEL(l), w);
    HIG::workarea_add_row (t, &row, l, w);

    // Gnome Keyring Option
    HIG :: workarea_add_section_spacer (t, row, 2);
    HIG :: workarea_add_section_title (t, &row, _("Password Storage"));
    w = new_check_button (
            _( "Save passwords in password storage"),
            "use-password-storage",
            USE_LIBSECRET_DEFAULT,
            prefs);
    HIG :: workarea_add_wide_control (t, &row, w);

  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Miscellaneous"), _("Miscellaneous"), "icon_prefs_extras.png", prefs));

  // Layout
  row = 0;
  t = HIG :: workarea_create ();
  HIG :: workarea_add_section_title (t, &row, _("Pane Layout"));
    std::string cur = _prefs.get_string ("pane-layout", "stacked-right");
    HIG :: workarea_add_section_spacer (t, row, 1);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    w = new_layout_radio (nullptr, "icon_layout_1.png", "stacked-top", cur, prefs);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    w = new_layout_radio (w, "icon_layout_2.png", "stacked-bottom", cur, prefs);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    w = new_layout_radio (w, "icon_layout_3.png", "stacked-left", cur, prefs);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    w = new_layout_radio (w, "icon_layout_4.png", "stacked-right", cur, prefs);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    w = new_layout_radio (w, "icon_layout_5.png", "stacked-vertical", cur, prefs);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    HIG::workarea_add_wide_control (t, &row, h);
  HIG :: workarea_add_section_divider (t, &row);
  HIG :: workarea_add_section_title (t, &row, _("Tasks"));
    HIG :: workarea_add_section_spacer (t, row, 6);
    cur = _prefs.get_string ("pane-orient", "groups,headers,body");
    w = new_orient_radio (nullptr, _("1=Groups, 2=Headers, 3=Body"), "groups,headers,body", cur, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_orient_radio (w, _("1=Groups, 2=Body, 3=Headers"), "groups,body,headers", cur, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_orient_radio (w, _("1=Headers, 2=Groups, 3=Body"), "headers,groups,body", cur, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_orient_radio (w, _("1=Headers, 2=Body, 3=Groups"), "headers,body,groups", cur, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_orient_radio (w, _("1=Body, 2=Groups, 3=Headers"), "body,groups,headers", cur, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);
    w = new_orient_radio (w, _("1=Body, 2=Headers, 3=Groups"), "body,headers,groups", cur, prefs);
    HIG :: workarea_add_wide_control (t, &row, w);

  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Layout"), _("Layout"), "icon_prefs_layout.png", prefs));

  // Headers
  row = 0;
  t = HIG :: workarea_create ();
  HIG :: workarea_add_section_title (t, &row, _("Header Pane Columns"));
    HIG :: workarea_add_section_spacer(t, row, 1);
    HIG :: workarea_add_wide_control (t, &row, header_columns_layout_new (prefs));
  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Headers"), _("Headers"), "icon_prefs_headers.png", prefs));

  // customizable actions
  row = 0;
  t = HIG :: workarea_create ();

    gtk_widget_set_tooltip_text (t, _("This menu lets you configure Pan to take certain actions on your behalf automatically, "
                                      "based on an article's score."));

    int i(0);
    GtkWidget** action_combo = new GtkWidget*[2];
    char* tmp = _("Mark affected articles read");
    action_combo[i++] = new_check_button (tmp, "rules-autocache-mark-read", false, prefs);
    action_combo[i++] = new_check_button (tmp, "rules-auto-dl-mark-read", false, prefs);

    i=0;
    w = score_handler_new (prefs, "rules-delete-value", "never", b);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    gtk_box_pack_start (GTK_BOX(h), gtk_label_new(nullptr), false, false, 0);
    HIG :: workarea_add_row (t, &row, _("_Delete articles scoring at: "), h);

    w = score_handler_new (prefs, "rules-mark-read-value", "never", b);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    gtk_box_pack_start (GTK_BOX(h), gtk_label_new(nullptr), false, false, 0);
    HIG :: workarea_add_row (t, &row, _("Mark articles read scoring at: "), h);

    w = score_handler_new (prefs, "rules-autocache-value", "never", b);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    gtk_box_pack_start (GTK_BOX(h), action_combo[i++], false, false, 0);
    HIG :: workarea_add_row (t, &row, _("_Cache articles scoring at: "), h);

    w = score_handler_new (prefs, "rules-auto-dl-value", "never", b);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
    gtk_box_pack_start (GTK_BOX(h), action_combo[i++], false, false, 0);
    HIG :: workarea_add_row (t, &row, _("Download attachments of articles scoring at: "), h);

  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Actions"), _("Actions"), "icon_prefs_actions.png", prefs));

  // Fonts
  row = 0;
  t = HIG :: workarea_create ();
  HIG :: workarea_add_section_title (t, &row, _("Fonts"));
    HIG :: workarea_add_section_spacer (t, row, 4);
    l = new_check_button (_("Use custom font in Group Pane:"), "group-pane-font-enabled", false, prefs);
    b = new_font_button ("group-pane-font", "Sans 10", prefs);
    g_signal_connect (l, "toggled", G_CALLBACK(font_toggled_cb), b);
    font_toggled_cb (GTK_TOGGLE_BUTTON(l), b);
    HIG :: workarea_add_row (t, &row, l, b);
    l = new_check_button (_("Use custom font in Header Pane:"), "header-pane-font-enabled", false, prefs);
    b = new_font_button ("header-pane-font", "Sans 10", prefs);
    g_signal_connect (l, "toggled", G_CALLBACK(font_toggled_cb), b);
    font_toggled_cb (GTK_TOGGLE_BUTTON(l), b);
    HIG :: workarea_add_row (t, &row, l, b);
    l = new_check_button (_("Use custom font in Body Pane:"), "body-pane-font-enabled", false, prefs);
    b = new_font_button ("body-pane-font", "Sans 10", prefs);
    g_signal_connect (l, "toggled", G_CALLBACK(font_toggled_cb), b);
    font_toggled_cb (GTK_TOGGLE_BUTTON(l), b);
    HIG :: workarea_add_row (t, &row, l, b);
    l = gtk_label_new_with_mnemonic (_("Monospace font:"));
    b = new_font_button ("monospace-font", "Monospace 10", prefs);
    HIG :: workarea_add_row (t, &row, l, b);
  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Fonts"), _("Fonts"), "icon_prefs_fonts.png", prefs));

  // default color theme's Colors
  PanColors const &colors(PanColors::get());
  char const *def_color_str(colors.def_bg.c_str());
  char const *def_color_fg_str(colors.def_fg.c_str());

  row = 0;
  t = HIG :: workarea_create ();
  HIG :: workarea_add_section_title (t, &row, _("Header Pane"));
    HIG :: workarea_add_section_spacer(t, row, 6);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-watched-fg", def_color_str, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-watched-bg", TANGO_CHAMELEON_LIGHT, prefs));
    HIG :: workarea_add_row (t, &row, _("Scores of 9999 or more:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-high-fg", def_color_str, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-high-bg", TANGO_BUTTER_LIGHT, prefs));
    HIG :: workarea_add_row (t, &row, _("Scores from 5000 to 9998:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-medium-fg", def_color_str, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-medium-bg", TANGO_SKY_BLUE_LIGHT, prefs));
    HIG :: workarea_add_row (t, &row, _("Scores from 1 to 4999:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-low-fg", TANGO_ALUMINUM_2, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-low-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("Scores from -9998 to -1:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-ignored-fg", TANGO_ALUMINUM_4, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("score-color-ignored-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("Scores of -9999 or less:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("color-read-fg", TANGO_ORANGE, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("color-read-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("Collapsed thread with unread articles:"), h);
  HIG :: workarea_add_section_divider (t, &row);
  HIG :: workarea_add_section_title (t, &row, _("Body Pane"));
    HIG :: workarea_add_section_spacer (t, row, 5);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-quote-1", TANGO_CHAMELEON_DARK, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-quote-1-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("First level of quoted text:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-quote-2", TANGO_ORANGE_DARK, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-quote-2-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("Second level of quoted text:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-quote-3", TANGO_PLUM_DARK, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-quote-3-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("Third level of quoted text:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-url", TANGO_SKY_BLUE_DARK, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-url-bg", def_color_str, prefs)); //
    HIG :: workarea_add_row (t, &row, _("URL:"), h);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-signature", TANGO_SKY_BLUE_LIGHT, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("body-pane-color-signature-bg", def_color_str, prefs)); //
    HIG :: workarea_add_row (t, &row, _("Signature:"), h);

  HIG :: workarea_add_section_divider (t, &row);
  HIG :: workarea_add_section_title (t, &row, _("Group Pane"));
    HIG :: workarea_add_section_spacer (t, row, 1);
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("group-pane-color-fg", def_color_fg_str, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("group-pane-color-bg", def_color_str, prefs)); //
    HIG :: workarea_add_row (t, &row, _("Group Color:"), h);

  // colors for others texts (score == 0 or body pane etc.... )
  HIG :: workarea_add_section_divider (t, &row);
  HIG :: workarea_add_section_title (t, &row, _("Other Text"));
    h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Text:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("text-color-fg", def_color_fg_str, prefs));
    pan_box_pack_start_defaults (GTK_BOX(h), gtk_label_new (_("Background:")));
    pan_box_pack_start_defaults (GTK_BOX(h), new_color_button ("text-color-bg", def_color_str, prefs));
    HIG :: workarea_add_row (t, &row, _("Text Color:"), h);
  HIG :: workarea_finish (t, &row);

  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Colors"), _("Colors"), "icon_prefs_colors.png", prefs));

  // Applications
  row = 0;
  t = HIG :: workarea_create ();
  HIG :: workarea_add_section_title (t, &row, _("Preferred Applications"));
    HIG :: workarea_add_section_spacer (t, row, 3);
    w = url_handler_new (prefs, "browser-mode", URL::get_environment(),
                                "custom-browser", "firefox", b);
    HIG :: workarea_add_row (t, &row, _("_Web browser:"), w);
    w = url_handler_new (prefs, "gemini-mode", URL::get_environment(),
                                "custom-gemini", "lagrange", b);
    HIG :: workarea_add_row (t, &row, _("_Gemini client:"), w);
    w = url_handler_new (prefs, "mailer-mode", URL::get_environment(),
                                "custom-mailer", "thunderbird", b);
    HIG :: workarea_add_row (t, &row, _("_Mail reader:"), w);
    w = editor_new (prefs);
    HIG :: workarea_add_row (t, &row, _("_Text editor:"), w);
    w = html_previewer_new (prefs);
    HIG :: workarea_add_row (t, &row, _("_HTML previewer:"), w);
  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Applications"), _("Applications"), "icon_prefs_applications.png", prefs));

  // Upload Options
  row = 0;
  t = HIG :: workarea_create ();
  HIG :: workarea_add_section_title (t, &row, _("Encoding"));
  HIG :: workarea_add_section_spacer (t, row, 4);
  // 16 MiB blocks max, 512 kb min
  w = new_spin_button ("upload-option-bpf", 512*1024, 1024*1024*16, prefs);
  l = gtk_label_new(_("Default bytes per file (for encoder): "));
  gtk_label_set_xalign (GTK_LABEL(l), 0.0);
  gtk_label_set_yalign (GTK_LABEL(l), 0.5);
  HIG::workarea_add_row (t, &row, l, w);

  HIG :: workarea_finish (t, &row);
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Upload"), _("Upload"), "icon_prefs_upload.png", prefs));

  // Hotkeys
  row = 0;
  t = HIG :: workarea_create ();
  shortcut_ptr_list = fill_pref_hotkeys(t, row, _prefs, this);

  HIG :: workarea_finish (t, &row);

  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), t, new_label_with_icon(_("_Shortcuts"), _("Shortcuts"), "icon_prefs_hotkeys.png", prefs));

  GtkWidget* scroll = gtk_scrolled_window_new (nullptr, nullptr);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll),
                                  GTK_POLICY_AUTOMATIC,
                                   GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_add_with_viewport (GTK_SCROLLED_WINDOW(scroll), notebook);

  gtk_widget_show_all (scroll);

  gtk_box_pack_start (GTK_BOX(gtk_dialog_get_content_area( GTK_DIALOG(dialog))), scroll, true, true, 0);

  _root = dialog;

  // initially set notebook to page 1 or last selected page from last visit
  gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), prefs.get_int("prefs-last-selected-page",1));

}
