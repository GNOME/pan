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
#include <fstream>
#include <iostream>
#include <sstream>
extern "C" {
  #include <gmime/gmime.h>
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
#ifdef HAVE_GTKSPELL
  #include <gtkspell/gtkspell.h>
#endif
}
#include <pan/general/debug.h>
#include <pan/general/file-util.h>
#include <pan/general/foreach.h>
#include <pan/general/log.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/usenet-utils/message-check.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/utf8-utils.h>
#include <pan/data/data.h>
#include <pan/tasks/task-post.h>
#include <pan/icons/pan-pixbufs.h>
#include "pad.h"
#include "hig.h"
#include "post-ui.h"
#include "post.ui.h"
#include "profiles-dialog.h"
#include "url.h"

#ifdef HAVE_GTKSPELL
#define DEFAULT_SPELLCHECK_FLAG true
#else
#define DEFAULT_SPELLCHECK_FLAG false
#endif

using namespace pan;

namespace
{
  bool remember_charsets (true);

  void on_remember_charset_toggled (GtkToggleAction * toggle, gpointer post_g)
  {
    remember_charsets = gtk_toggle_action_get_active (toggle);
  }

  void on_spellcheck_toggled (GtkToggleAction * toggle, gpointer post_g)
  {
    const bool enabled = gtk_toggle_action_get_active (toggle);
    static_cast<PostUI*>(post_g)->set_spellcheck_enabled (enabled);
  }
}

void
PostUI :: set_spellcheck_enabled (bool enabled)
{
  _prefs.set_flag ("spellcheck-enabled", enabled);

  if (enabled)
  {
#ifdef HAVE_GTKSPELL
    GtkTextView * view = GTK_TEXT_VIEW(_body_view);
    GError * err (0);
    const char * locale = NULL;
    gtkspell_new_attach (view, locale, &err);
    if (err) {
      Log::add_err_va (_("Error setting spellchecker: %s"), err->message);
      g_clear_error (&err);
    }
#else
    GtkWidget * w = gtk_message_dialog_new_with_markup (
      GTK_WINDOW(_root),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      _("<b>Spellchecker not found!</b>\n \nWas this copy of Pan compiled with GtkSpell enabled?"));
    g_signal_connect_swapped (w, "response", G_CALLBACK (gtk_widget_destroy), w);
    gtk_widget_show_all (w);
#endif
  }
  else // disable
  {
#ifdef HAVE_GTKSPELL
    GtkTextView * view = GTK_TEXT_VIEW(_body_view);
    GtkSpell * spell = gtkspell_get_from_text_view (view);
    if (spell)
      gtkspell_detach (spell);
#endif
  }
}

/***
****  WRAP CODE
***/

namespace
{
  bool wrap_is_enabled (true);

  void on_wrap_toggled (GtkToggleAction* toggle, gpointer unused)
  {
    wrap_is_enabled = gtk_toggle_action_get_active (toggle);
  }

  void
  text_was_inserted_cb (GtkTextBuffer   * text_buffer,
                        GtkTextIter     * insert_pos,
                        char            * text,
                        int               text_len,
                        gpointer          tm_gpointer)
  {
    // dampen out a recursive event storm that would otherwise
    // happen from us changing the text buffer by rewrapping the
    // text here
    static bool dampen_feedback (false);

    if (dampen_feedback || !wrap_is_enabled)
      return;

    GtkTextIter line_begin_iter;
    const TextMassager& tm (*static_cast<TextMassager*>(tm_gpointer));
    const int line_number = gtk_text_iter_get_line (insert_pos);

    // make sure the line length isn't too wide
    gtk_text_buffer_get_iter_at_line (text_buffer, &line_begin_iter, line_number);
    int line_length = gtk_text_iter_get_chars_in_line(&line_begin_iter);
    if (line_length < tm.get_wrap_column())
      return;

    // we'll need to wrap all the way to the end of the paragraph to ensure
    // the wrapping works right -- so look for an empty line...
    GtkTextIter end_iter = line_begin_iter;
    for (;;) {
      if (!gtk_text_iter_forward_line (&end_iter))
        break;
      if (gtk_text_iter_get_chars_in_line (&end_iter) < 2)
        break;
    }

    // move backward to the end of the previous word
    gtk_text_iter_backward_word_start (&end_iter);
    gtk_text_iter_forward_word_end (&end_iter);

    // if the wrapped text is any different from the text already in
    // the buffer, then replace the buffer's text with the wrapped version.
    char * pch (gtk_text_buffer_get_text (text_buffer, &line_begin_iter, &end_iter, false));
    std::string before = pch;
    g_free (pch);
    std::string after (tm.fill (before));
    if (before == after)
      return;

    // okay, we need to rewrap.
    dampen_feedback = true;

    // remember where the insert pos was so that we can revalidate
    // the iterator that was passed in
    int insert_iterator_pos (gtk_text_iter_get_offset (insert_pos));

    // get char_offset so that we can update insert and selection_bound
    // marks after we make the change
    GtkTextMark * mark (gtk_text_buffer_get_mark (text_buffer, "insert"));
    GtkTextIter insert_iter;
    gtk_text_buffer_get_iter_at_mark (text_buffer, &insert_iter, mark);
    int char_offset (gtk_text_iter_get_offset (&insert_iter));
    const int width_diff ((int)after.size() - (int)before.size());

    // swap the non-wrapped for the wrapped text
    gtk_text_buffer_delete (text_buffer, &line_begin_iter, &end_iter);
    gtk_text_buffer_insert (text_buffer, &line_begin_iter, after.c_str(), -1);

    // update the insert and selection_bound marks to
    // where they were before the swap
    gtk_text_buffer_get_iter_at_offset (text_buffer, &insert_iter, char_offset + width_diff);
    mark = gtk_text_buffer_get_mark (text_buffer, "insert");
    gtk_text_buffer_move_mark (text_buffer, mark, &insert_iter);
    mark = gtk_text_buffer_get_mark (text_buffer, "selection_bound");
    gtk_text_buffer_move_mark (text_buffer, mark, &insert_iter);

    // revalidate the insert_pos iterator
    gtk_text_buffer_get_iter_at_offset (text_buffer, insert_pos, insert_iterator_pos + width_diff);

    dampen_feedback = false;
  }
}


/***
****  Menu and Toolbar
***/

namespace
{
  GtkWidget* get_focus (gpointer p) { return gtk_window_get_focus(GTK_WINDOW(static_cast<PostUI*>(p)->root())); }

  void do_cut      (GtkAction * w, gpointer p) { g_signal_emit_by_name (get_focus(p), "cut_clipboard"); }
  void do_copy     (GtkAction * w, gpointer p) { g_signal_emit_by_name (get_focus(p), "copy_clipboard"); }
  void do_paste    (GtkAction * w, gpointer p) { g_signal_emit_by_name (get_focus(p), "paste_clipboard"); }
  void do_rot13    (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->rot13_selection(); }
  void do_wrap     (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->wrap_body (); }
  void do_edit     (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->spawn_editor (); }
  void do_editors  (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->manage_editors (); }
  void do_profiles (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->manage_profiles (); }
  void do_send     (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->send_now (); }
  void do_save     (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->save_draft (); }
  void do_open     (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->open_draft (); }
  void do_close    (GtkAction * w, gpointer p) { static_cast<PostUI*>(p)->close_window (); }

  void editor_selected_cb (GtkRadioAction * action, GtkRadioAction * current, gpointer post_gpointer)
  {
    if (action == current)
    {
      const char * command = (const char*) g_object_get_data (G_OBJECT(action), "editor-command");
      static_cast<PostUI*>(post_gpointer)->set_editor_command (command);
    }
  }

  GtkActionEntry entries[] =
  {
    { "file-menu", NULL, N_("_File") },
    { "edit-menu", NULL, N_("_Edit") },
    { "profile-menu", NULL, N_("_Profile") },
    { "charsets-menu", NULL, N_("Character Encoding") },
    { "editors-menu", NULL, N_("Set Editor") },
    { "post-toolbar", NULL, "post" },
    { "post-article", GTK_STOCK_EXECUTE, N_("_Send Article"), "<control>Return", N_("Send Article Now"), G_CALLBACK(do_send) },
    { "save-draft", GTK_STOCK_SAVE, N_("Sa_ve Draft"), "<control>s", N_("Save as a Draft for Future Posting"), G_CALLBACK(do_save) },
    { "open-draft", GTK_STOCK_OPEN, N_("_Open Draft..."), "<control>o", N_("Open an Article Draft"), G_CALLBACK(do_open) },
    { "rewrap", GTK_STOCK_JUSTIFY_FILL, N_("Wrap _Now"), NULL, N_("Wrap the Article Body to 80 Columns"), G_CALLBACK(do_wrap) },
    { "close", GTK_STOCK_CLOSE, NULL, NULL, NULL, G_CALLBACK(do_close) },
    { "cut", GTK_STOCK_CUT, NULL, NULL, NULL, G_CALLBACK(do_cut) },
    { "copy", GTK_STOCK_COPY, NULL, NULL, NULL, G_CALLBACK(do_copy) },
    { "paste", GTK_STOCK_PASTE, NULL, NULL, NULL, G_CALLBACK(do_paste) },
    { "rot13", GTK_STOCK_REFRESH, N_("_Rot13"), NULL, N_("Rot13 Selected Text"), G_CALLBACK(do_rot13) },
    { "run-editor", GTK_STOCK_JUMP_TO, N_("Run _Editor"), "<control>e", NULL, G_CALLBACK(do_edit) },
    { "manage-editors", NULL, N_("_Manage Editor List..."), NULL, NULL, G_CALLBACK(do_editors) },
    { "manage-profiles", NULL, N_("Manage Posting Pr_ofiles..."), NULL, NULL, G_CALLBACK(do_profiles) }
  };

  GtkToggleActionEntry toggle_entries[] =
  {
    { "wrap", GTK_STOCK_JUSTIFY_FILL, N_("_Wrap Text"), NULL, NULL, G_CALLBACK(on_wrap_toggled), true },
    { "remember-charset", NULL, N_("Remember _Charset for This Group"), NULL, NULL, G_CALLBACK(on_remember_charset_toggled), true },
    { "spellcheck", NULL, N_("Check _Spelling"), NULL, NULL, G_CALLBACK(on_spellcheck_toggled), true }
  };

  void add_widget (GtkUIManager* merge, GtkWidget* widget, gpointer vbox)
  {
    if (GTK_IS_TOOLBAR (widget)) {
      GtkWidget * handle_box = gtk_handle_box_new ();
      gtk_widget_show (handle_box);
      gtk_container_add (GTK_CONTAINER (handle_box), widget);
      g_signal_connect_swapped (widget, "destroy", G_CALLBACK (gtk_widget_destroy), handle_box);
      widget = handle_box;
    }
    gtk_box_pack_start (GTK_BOX(vbox), widget, false, false, 0);
  }

  bool app_is_toggling_charset_action (false);
}

void
PostUI :: charset_selected_cb_static (GtkRadioAction * action, GtkRadioAction * current, gpointer ui_gpointer)
{
  if (action == current)
  {
    const char * charset = (const char*) g_object_get_data (G_OBJECT(action), "charset");
    static_cast<PostUI*>(ui_gpointer)->charset_selected_cb (charset);
  }
}

void
PostUI :: charset_selected_cb (const char * charset)
{
  // since user has specified a charset manually,
  // we need to stop implicitly picking one
  // whenever the newsgroups field changes.
  if (_group_entry_changed_id && !app_is_toggling_charset_action) {
    g_signal_handler_disconnect (_groups_entry, _group_entry_changed_id);
    _group_entry_changed_id = 0;
    if (_group_entry_changed_idle_tag) {
      g_source_remove (_group_entry_changed_idle_tag);
      _group_entry_changed_idle_tag = 0;
    }
  }

  // set the charset
  set_charset (charset);
}

#define DEFAULT_CHARSET  "UTF-8"

void
PostUI :: add_charset_list ()
{
  const struct CharsetStruct {
    const char *charset, *name;
  } charsets[] = {
    {"ISO-8859-4",   N_("Baltic")},
    {"ISO-8859-13",  N_("Baltic")},
    {"windows-1257", N_("Baltic")},
    {"ISO-8859-2",   N_("Central European")},
    {"windows-1250", N_("Central European")},
    {"gb2312",       N_("Chinese Simplified")},
    {"big5",         N_("Chinese Traditional")},
    {"ISO-8859-5",   N_("Cyrillic")},
    {"windows-1251", N_("Cyrillic")},
    {"KOI8-R",       N_("Cyrillic")},
    {"KOI8-U",       N_("Cyrillic, Ukrainian")},
    {"ISO-8859-7",   N_("Greek")},
    {"ISO-2022-jp",  N_("Japanese")},
    {"euc-kr",       N_("Korean")},
    {"ISO-8859-9",   N_("Turkish")},
    {"ISO-8859-1",   N_("Western")},
    {"ISO-8859-15",  N_("Western, New")},
    {"windows-1252", N_("Western")},
    {"UTF-8",        N_("Unicode, UTF-8")}
  };

  GtkRadioAction * radio_group (0);
  for (int i(0), count(G_N_ELEMENTS(charsets)); i<count; ++i)
  {
    char * label (g_strdup_printf ("%s (%s)", _(charsets[i].name), charsets[i].charset));
    char * unique_name (g_strdup_printf ("charset-%s", charsets[i].charset));
    GtkAction * o ((GtkAction*) g_object_new (GTK_TYPE_RADIO_ACTION,
      "name", unique_name,
      "label", label,
      "value", i,
      "group", radio_group, NULL));
    g_object_set_data_full (G_OBJECT(o), "charset", g_strdup(charsets[i].charset), g_free);
    radio_group = GTK_RADIO_ACTION (o);
    const bool match (!strcmp (DEFAULT_CHARSET, charsets[i].charset));
    if (match) {
      const bool old_val (app_is_toggling_charset_action);
      app_is_toggling_charset_action = true;
      gtk_toggle_action_set_active (GTK_TOGGLE_ACTION(o), true);
      app_is_toggling_charset_action = old_val;
    }
    g_signal_connect (o, "changed", G_CALLBACK(charset_selected_cb_static), this);
    gtk_action_group_add_action (_agroup, o);
    g_object_unref (o);

    const guint id (gtk_ui_manager_new_merge_id (_uim));
    gtk_ui_manager_add_ui (_uim, id,
                           "/post/edit-menu/charsets-menu/charset-list",
                            label, unique_name,
                            GTK_UI_MANAGER_MENUITEM, false);
    g_free (unique_name);
    g_free (label);
  }
}

typedef Profiles::strings_t strings_t;

void
PostUI :: add_editor_list ()
{
  // remove the old editor buttons...
  typedef std::set<guint> unique_uints_t;
  static unique_uints_t editor_ui_ids;
  foreach_const (unique_uints_t, editor_ui_ids, it)
    gtk_ui_manager_remove_ui (_uim, *it);

  // add the new editor buttons...
  strings_t commands;
  _profiles.get_editors (commands);
  const std::string& editor_command (_profiles.get_active_editor ());
  GtkRadioAction * radio_group (0);
  for (size_t i=0; i<commands.size(); ++i)
  {
    const char * cmd (commands[i].c_str());
    char name[512], verb[512];
    g_snprintf (name, sizeof(name), "%s", cmd);
    g_snprintf (verb, sizeof(verb), "Edit With %s", cmd);

    GtkAction * o = (GtkAction*) g_object_new (GTK_TYPE_RADIO_ACTION,
       "name", verb,
       "label", name,
       "value", i,
       "group", radio_group, NULL);
    g_object_set_data_full (G_OBJECT(o), "editor-command", g_strdup(cmd), g_free);
    radio_group = GTK_RADIO_ACTION (o);
    const bool match (editor_command == cmd);
    if (match)
      gtk_toggle_action_set_active (GTK_TOGGLE_ACTION(o), true);
    g_signal_connect (o, "changed", G_CALLBACK(editor_selected_cb), this);
    gtk_action_group_add_action (_agroup, o);
    g_object_unref (o);


    const guint id (gtk_ui_manager_new_merge_id (_uim));
    gtk_ui_manager_add_ui (_uim, id,
                           "/post/edit-menu/editors-menu/editor-list",
                            name, verb,
                            GTK_UI_MANAGER_MENUITEM, false);
    editor_ui_ids.insert (id);
  }
}

void
PostUI :: add_actions (GtkWidget * box)
{
  _uim = gtk_ui_manager_new ();

  // read the file...
  char * filename = g_build_filename (file::get_pan_home().c_str(), "post.ui", NULL);
  GError * err (0);
  if (!gtk_ui_manager_add_ui_from_file (_uim, filename, &err)) {
    g_clear_error (&err);
    gtk_ui_manager_add_ui_from_string (_uim, fallback_post_ui, -1, &err);
  }
  if (err) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), filename, err->message);
    g_clear_error (&err);
  }
  g_free (filename);

  g_signal_connect (_uim, "add_widget", G_CALLBACK(add_widget), box);

  // add the actions...
  _agroup = gtk_action_group_new ("post");
  gtk_action_group_set_translation_domain (_agroup, NULL);
  gtk_action_group_add_actions (_agroup, entries, G_N_ELEMENTS(entries), this);
  gtk_action_group_add_toggle_actions (_agroup, toggle_entries, G_N_ELEMENTS(toggle_entries), this);
  gtk_ui_manager_insert_action_group (_uim, _agroup, 0);

  add_editor_list ();
  add_charset_list ();
}

void
PostUI :: set_editor_command (const StringView& command)
{
  _profiles.set_active_editor (command);
}

void
PostUI :: set_charset (const StringView& charset)
{
  const std::string new_charset (charset);

  if (_charset != new_charset)
  {
    _charset = new_charset;

    std::string unique_name ("charset-");
    unique_name.append (charset.str, charset.len);
    GtkAction * action = gtk_action_group_get_action (_agroup, unique_name.c_str());
    gtk_toggle_action_set_active (GTK_TOGGLE_ACTION(action), true);
  }
}

void
PostUI :: manage_profiles ()
{
  ProfilesDialog d (_data, _profiles, GTK_WINDOW(_root));
  gtk_dialog_run (GTK_DIALOG(d.root()));
  gtk_widget_destroy (d.root());
  update_profile_combobox ();
  apply_profile ();
}

namespace
{
  std::string get_text (GtkTextBuffer * buf)
  {
    std::string ret;

    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buf, &start, &end);
    char * pch (gtk_text_buffer_get_text (buf, &start, &end, false));
    if (pch)
      ret = pch;
    g_free (pch);

    return ret;
  }
}

void
PostUI :: manage_editors ()
{
  GtkWidget * d = gtk_dialog_new_with_buttons (
    _("Manage Editor List"),
    GTK_WINDOW(_root),
    (GtkDialogFlags)(GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT),
    GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
    NULL);

  char b[512], lab[512];
  g_snprintf (b, sizeof(b), _("Editors"));
  g_snprintf (lab, sizeof(lab), "        <b>%s:</b>        ", b);
  GtkWidget * w = gtk_label_new (NULL);
  gtk_label_set_markup (GTK_LABEL(w), lab);
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(d)->vbox), w, false, false, 0);
  gtk_widget_show (w);

  std::string s;
  strings_t commands;
  _profiles.get_editors (commands);
  foreach_const (strings_t, commands, it)
    s += *it + "\n";

  GtkWidget * frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);

  GtkTextBuffer * buf = gtk_text_buffer_new (NULL);
  gtk_text_buffer_set_text (buf, s.c_str(), s.size());
  w = gtk_text_view_new_with_buffer (buf);
  gtk_container_add (GTK_CONTAINER(frame), w);
  gtk_box_pack_start_defaults (GTK_BOX(GTK_DIALOG(d)->vbox), frame);
  gtk_container_set_border_width (GTK_CONTAINER(frame), PAD_BIG);
  gtk_widget_show_all (frame);

  gtk_dialog_run (GTK_DIALOG (d));

  const std::string text (get_text (buf));
  commands.clear ();
  StringView token, v (text);
  while (v.pop_token (token, '\n')) {
    token.trim ();
    if (!token.empty())
      commands.push_back (token.to_string());
  }

  _profiles.set_editors (commands);
  add_editor_list ();

  gtk_widget_destroy (d);
}

void
PostUI :: rot13_selection ()
{
  GtkTextIter start, end;
  if (gtk_text_buffer_get_selection_bounds (_body_buf, &start, &end))
  {
    char * str (gtk_text_buffer_get_text (_body_buf, &start, &end, false));
    _tm.rot13_inplace (str);
    gtk_text_buffer_delete (_body_buf, &start, &end);
    gtk_text_buffer_insert (_body_buf, &start, str, strlen(str));
    g_free (str);
  }
} 

void
PostUI :: wrap_body ()
{
  // get the current body
  GtkTextBuffer * buffer (_body_buf);
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buffer, &start, &end);
  char * body = gtk_text_buffer_get_text (buffer, &start, &end, false);

  // wrap the body
  const std::string new_body (_tm.fill( body));

  // turn off our own wrapping while we fill the body pane */
  const bool b (wrap_is_enabled);
  wrap_is_enabled = false;
  gtk_text_buffer_set_text (_body_buf, new_body.c_str(), new_body.size());
  wrap_is_enabled = b;

  // cleanup
  g_free (body);
}

namespace
{
  gboolean delete_event_cb (GtkWidget *w, GdkEvent *e, gpointer user_data)
  {
    static_cast<PostUI*>(user_data)->close_window ();
    return true; // don't invoke the default handler that destroys the widget
  }
}

void
PostUI :: close_window ()
{
  bool destroy_flag (false);;

  if (get_text (_body_buf) == _unchanged_body)
    destroy_flag = true;

  else {
    GtkWidget * d = gtk_message_dialog_new (
      GTK_WINDOW(_root),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, NULL);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
      _("Your changes will be lost!"),
      _("Close this window and lose your changes?"));
    gtk_dialog_add_buttons (GTK_DIALOG(d),
                            GTK_STOCK_GO_BACK, GTK_RESPONSE_NO,
                            GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
                            NULL);
    gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_NO);
    destroy_flag = gtk_dialog_run (GTK_DIALOG(d)) == GTK_RESPONSE_CLOSE;
    gtk_widget_destroy (d);
  }

  if (destroy_flag)
    gtk_widget_destroy (_root);
}

bool
PostUI :: check_message (const Quark& server, GMimeMessage * msg)
{
  MessageCheck :: unique_strings_t errors;
  MessageCheck :: Goodness goodness;

  quarks_t groups_this_server_has;
  _gs.server_get_groups (server, groups_this_server_has);
  MessageCheck :: message_check (msg, _hidden_headers["X-Draft-Attribution"], groups_this_server_has, errors, goodness);

  if (goodness.is_ok())
    return true;

  std::string s;
  foreach_const (MessageCheck::unique_strings_t, errors, it)
    s += *it + "\n";
  s.resize (s.size()-1); // eat trailing linefeed

  const GtkMessageType type (goodness.is_refuse() ? GTK_MESSAGE_ERROR : GTK_MESSAGE_WARNING);
  GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          type, GTK_BUTTONS_NONE, NULL);
  HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
                           _("There were problems with this post."),
                           s.c_str());
  gtk_dialog_add_button (GTK_DIALOG(d), _("Go Back"), GTK_RESPONSE_CANCEL);
  if (goodness.is_warn())
    gtk_dialog_add_button (GTK_DIALOG(d), _("Post Anyway"), GTK_RESPONSE_APPLY);
  const int response = gtk_dialog_run (GTK_DIALOG(d));
  gtk_widget_destroy (d);
  return response == GTK_RESPONSE_APPLY;
}

bool
PostUI :: check_charset ()
{
  const std::string charset (!_charset.empty() ? _charset : "UTF-8");
  // GtkTextBuffer is in UTF-8, so posting in UTF-8 is ok
  if (charset == "UTF-8")
    return true;

  // Check if body can be posted in the selected charset 
  std::string body = get_text(_body_buf);
  char *tmp = g_convert (body.c_str(), -1, charset.c_str(), "UTF-8", NULL, NULL, NULL);
  if (tmp) {
    g_free(tmp);
    return true;
  }

  // Wrong charset. Let GMime guess the best charset.
  const char * best_charset = g_mime_charset_best (body.c_str(), strlen (body.c_str()));
  if (best_charset == NULL) best_charset = "ISO-8859-1";
  // GMime reports (some) charsets in lower case. Pan always uses uppercase.
  tmp = g_ascii_strup (best_charset, -1);

  // Prompt the user
  char * msg = g_strdup_printf (_("Message uses characters not specified in charset '%s' - possibly use '%s' "), charset.c_str(), tmp);
  GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_ERROR, GTK_BUTTONS_NONE, 
										  NULL);
  HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
                           _("There were problems with this post."),
                           msg);
  gtk_dialog_add_button (GTK_DIALOG(d), _("Go Back"), GTK_RESPONSE_CANCEL);
  gtk_dialog_run (GTK_DIALOG(d));
  gtk_widget_destroy (d);
  g_free (tmp);
  g_free (msg);
  
  return false;
}


namespace
{
  GtkWidget * new_go_online_button ()
  {
    GtkWidget * button = gtk_button_new ();
    GtkWidget * label = gtk_label_new_with_mnemonic (_("Go _Online"));
    GtkWidget * image = gtk_image_new_from_stock (GTK_STOCK_NETWORK, GTK_ICON_SIZE_BUTTON);
    GtkWidget * hbox = gtk_hbox_new (false, 2);
    GtkWidget * align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
    gtk_box_pack_start (GTK_BOX (hbox), image, false, false, 0);
    gtk_box_pack_end (GTK_BOX (hbox), label, false, false, 0);
    gtk_container_add (GTK_CONTAINER (align), hbox);
    gtk_container_add (GTK_CONTAINER (button), align);
    gtk_widget_show_all (button);
    return button;
  }
}

namespace
{
  gboolean pulse_me (gpointer progressbar)
  {
    gtk_progress_bar_pulse (GTK_PROGRESS_BAR (progressbar));
    return true;
  }
  void remove_progress_tag (gpointer tag)
  {
    g_source_remove (GPOINTER_TO_UINT(tag));
  }
}

namespace
{
#if !GTK_CHECK_VERSION(2,6,0)
  char* gtk_combo_box_get_active_text (GtkComboBox * combo_box) {
    char * text (0);
    GtkTreeIter  iter;
    if (gtk_combo_box_get_active_iter (combo_box, &iter)) {
      GtkTreeModel * model = gtk_combo_box_get_model (combo_box);
      gtk_tree_model_get (model, &iter, 0, &text, -1);
    }
    return text;
  }
#endif
}

void
PostUI :: send_now ()
{
  if (!check_charset())
    return;
  GMimeMessage * message (new_message_from_ui (POSTING));
  if (!maybe_post_message (message))
    g_object_unref (G_OBJECT(message));
}

void
PostUI :: done_sending_message (GMimeMessage * message, bool ok)
{
  if (ok) {
    _unchanged_body = get_text (_body_buf);
    close_window ();
  }

  g_object_unref (G_OBJECT(message));
}

namespace
{
  // http://www.rfc-editor.org/rfc/rfc1738.txt
  std::string encode (const StringView& in)
  {
    static std::set<char> keep;
    if (keep.empty()) {
      for (int i='0'; i<='9'; ++i) keep.insert(i); // always okay
      for (int i='a'; i<='z'; ++i) keep.insert(i); // always okay
      for (int i='A'; i<='Z'; ++i) keep.insert(i); // always okay
      for (char *pch="$-_.+!*'(),"; *pch; ++pch) keep.insert(*pch); // okay in the right context
      for (char *pch="$&+,/:;=?@"; *pch; ++pch) keep.erase(*pch); // reserved
      for (char *pch="()"; *pch; ++pch) keep.erase(*pch); // gives thunderbird problems?
    }

    std::string out;
    foreach_const (StringView, in, pch) {
      if (keep.count(*pch))
        out += *pch;
      else {
        char buf[8];
        g_snprintf (buf, sizeof(buf), "%%%02x", (int)*pch);
        out += buf;
      }
    }
    return out;
  }
}

void
PostUI :: maybe_mail_message (GMimeMessage * message)
{
  std::string url, to, groups;
  gboolean unused;
  char * headers (g_mime_message_get_headers (message));
  char * body (g_mime_message_get_body (message, true, &unused));
  StringView key, val, v(headers);
  v.trim ();
  while (v.pop_token (val, '\n') && val.pop_token(key,':')) {
    key.trim ();
    val.eat_chars (1);
    val.trim ();
    std::string key_enc (encode (key));
    std::string val_enc (encode (val));
    if (key == "To")
      to = val_enc;
    else if (key == "Newsgroups")
      groups = val;
    else if (key!="User-Agent" && key!="Mime-Version" && key!="Content-Type") {
      url += '&';
      url += key_enc;
      url += '=';
      url += val_enc;
    }
  }

  if (!groups.empty()) {
    char * pch = g_strdup_printf ("[This mail was also posted to %s.]\n\n%s", groups.c_str(), body);
    g_free (body);
    body = pch;
  }

  url += std::string("&body=") + encode(body);
  url[0] = '?';
  url = std::string("mailto:") + to + url;

  if (!to.empty())
    URL :: open (_prefs, url.c_str(), URL::MAIL);
  done_sending_message (message, true);

  g_free (body);
  g_free (headers);
}

void
PostUI :: on_progress_finished (Progress&, int status) // posting finished
{
  _post_task->remove_listener (this);
  gtk_widget_destroy (_post_dialog);

  GMimeMessage * message (_post_task->get_message ());
  if (status != OK) // error posting.. stop.
    done_sending_message (message, false);
  else
    maybe_mail_message (message);
}

void
PostUI :: on_progress_error (Progress&, const StringView& message)
{
  GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                          GTK_DIALOG_DESTROY_WITH_PARENT,
                                          GTK_MESSAGE_ERROR,
                                          GTK_BUTTONS_CLOSE, "%s", message.to_string().c_str());
  g_signal_connect_swapped (d, "response", 
                            G_CALLBACK(gtk_widget_destroy), d);
  gtk_widget_show (d);
}

bool
PostUI :: maybe_post_message (GMimeMessage * message)
{
  /**
  ***  Find the server to use
  **/

  // get the profile...
  Profile profile;
  char * pch = gtk_combo_box_get_active_text (GTK_COMBO_BOX(_from_combo));
  std::string s;
  if (pch) {
    _profiles.get_profile (pch, profile);
    profile.get_from_header (s);
    g_free (pch);
  }
  // get the server associated with that profile...
  const Quark& server (profile.posting_server);
  // if the server's invalid, bitch about it to the user
  if (server.empty() || !_data.get_servers().count(server)) {
    GtkWidget * d = gtk_message_dialog_new (
      GTK_WINDOW(_root),
      GTK_DIALOG_DESTROY_WITH_PARENT,
      GTK_MESSAGE_ERROR,
      GTK_BUTTONS_CLOSE,
      _("No posting server is set for this posting profile.\nPlease edit the profile via Edit|Manage Posting Profiles."));
    gtk_dialog_run (GTK_DIALOG(d));
    gtk_widget_destroy (d);
    return false;
  }

  /**
  ***  Make sure the message is OK...
  **/

  if (!check_message (server, message))
    return false;

  /**
  *** If this is email only, skip the rest of the posting...
  *** we only stayed this long to get check_message()
  **/
  const StringView groups (g_mime_message_get_header (message, "Newsgroups"));
  if (groups.empty()) {
    maybe_mail_message (message);
    return true;
  }

  /**
  ***  Make sure we're online...
  **/
  if (!_queue.is_online())
  {
    GtkWidget * d = gtk_message_dialog_new (GTK_WINDOW(_root),
                                            GTK_DIALOG_DESTROY_WITH_PARENT,
                                            GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE, NULL);
    HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
                             _("Pan is Offline."),
                             _("Go online to post the article?"));
    gtk_dialog_add_button (GTK_DIALOG(d), GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL);
    gtk_dialog_add_action_widget (GTK_DIALOG(d), new_go_online_button(), GTK_RESPONSE_OK);
    const int response = gtk_dialog_run (GTK_DIALOG(d));
    gtk_widget_destroy (d);
    if (response !=  GTK_RESPONSE_OK)
      return false;
    _queue.set_online (true);
  }

  /**
  ***  Pop up a ``Posting'' Dialog...
  **/
  GtkWidget * d = gtk_dialog_new_with_buttons (_("Posting Article"),
                                               GTK_WINDOW(_root),
                                               GTK_DIALOG_DESTROY_WITH_PARENT,
                                               //GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               NULL);
  char buf[512];
  g_snprintf (buf, sizeof(buf), "<b>%s</b>", _("Posting..."));
  GtkWidget * w = GTK_WIDGET (g_object_new (GTK_TYPE_LABEL, "use-markup", TRUE, "label", buf, NULL));
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(d)->vbox), w, false, false, PAD_SMALL);
  w = gtk_progress_bar_new ();
  gtk_progress_bar_set_pulse_step (GTK_PROGRESS_BAR(w), 0.05);
  const guint tag = g_timeout_add (100, pulse_me, w);
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(d)->vbox), w, false, false, PAD_SMALL);
  g_object_set_data_full (G_OBJECT(d), "progressbar-timeout-tag", GUINT_TO_POINTER(tag), remove_progress_tag);
  _post_dialog = d;
  g_signal_connect (_post_dialog, "destroy", G_CALLBACK(gtk_widget_destroyed), &_post_dialog);
  gtk_widget_show_all (d);
  _post_task = new TaskPost (server, message);
  _post_task->add_listener (this);
  
  _queue.add_task (_post_task, Queue::TOP);

  /**
  ***  Maybe remember the charsets.
  **/
  if (remember_charsets) {
    const char * text = gtk_entry_get_text (GTK_ENTRY(_groups_entry));
    StringView line(text), groupname;
    while (line.pop_token (groupname, ',')) {
      groupname.trim ();
      if (!groupname.empty())
        _group_prefs.set_string (groupname, "character-encoding", _charset);
    }
  }

  return true;
}

/***
****
***/

void
PostUI :: spawn_editor ()
{
  GtkTextBuffer * buf (_body_buf);
  bool ok (true);

  // open a new tmp file
  char * fname (0);
  FILE * fp (0);
  if (ok) {
    GError * err = NULL;
    const int fd (g_file_open_tmp ("pan_edit_XXXXXX", &fname, &err));
    if (fd != -1)
      fp = fdopen (fd, "w");
    else {
      Log::add_err (err && err->message ? err->message : _("Error opening temporary file"));
      if (err)
        g_clear_error (&err);
      ok = false;
    }
  }

  // get the current article body
  char * chars (0);
  size_t len;
  if (ok) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buf, &start, &end);
    chars = gtk_text_buffer_get_text (buf, &start, &end, false);
    len = strlen (chars);
  }

  if (ok) {
    if (fwrite (chars, sizeof(char), len, fp) != len) {
      ok = false;
      Log::add_err_va (_("Error writing article to temporary file: %s"), g_strerror(errno));
    }
  }

  if (fp != NULL) {
    fclose (fp);
    fp = NULL;
  }

  g_free (chars);

  // parse the command line
  int argc (0);
  char ** argv (0);
  if (ok) {
    const std::string& editor (_profiles.get_active_editor());
    GError * err (0);
    g_shell_parse_argv (editor.c_str(), &argc, &argv, &err);
    if (err != NULL) {
      Log::add_err_va (_("Error parsing \"external editor\" command line: %s (Command was: %s)"), err->message, editor.c_str());
      g_clear_error (&err);
      ok = false;
    }
  }

  // put temp file's name into the substitution
  bool filename_added (false);
  for (int i=0; i<argc; ++i) {
    char * token (argv[i]);
    char * sub (strstr (token, "%t"));
    if (sub) {
      GString * gstr  = g_string_new (0);
      g_string_append_len (gstr, token, sub-token);
      g_string_append (gstr, fname);
      g_string_append (gstr, sub+2);
      g_free (token);
      argv[i] = g_string_free (gstr, false);
      filename_added = true;
    }
  }

  // no substitution field -- add the filename at the end
  if (!filename_added) {
    char ** v = g_new (char*, argc+2);
    for (int i=0; i<argc; ++i)
      v[i] = argv[i];
    v[argc++] = g_strdup (fname);
    v[argc] = NULL;
    g_free (argv);
    argv = v;
  }

  // spawn off the external editor
  if (ok) {
    GError * err (0);
    g_spawn_sync (0, argv, 0, G_SPAWN_SEARCH_PATH, 0, 0, 0, 0, 0, &err);
    if (err != NULL) {
      Log::add_err_va (_("Error starting external editor: %s"), err->message);
      g_clear_error (&err);
      ok = false;
    }
  }

  // read the file contents back in
  if (ok) {
    GError * err (0);
    char * body (0);
    gsize body_len (0);
    g_file_get_contents (fname, &body, &body_len, &err);
    if (err != NULL) {
      Log::add_err_va (_("Error reading file \"%s\": %s"), err->message, g_strerror(errno));
      g_clear_error (&err);
      ok = false;
    } else {
      GtkTextIter start, end;
      gtk_text_buffer_get_bounds (buf, &start, &end);
      gtk_text_buffer_delete (buf, &start, &end);
      gtk_text_buffer_insert (buf, &start, body, body_len);
    }
    g_free (body);
  }

  // cleanup
  ::remove (fname);
  g_free (fname);
  g_strfreev (argv);
}

namespace
{
  std::string& get_draft_filename ()
  {
    static std::string fname;

    if (fname.empty())
    {
      fname = file::get_pan_home ();
      char * pch = g_build_filename (fname.c_str(), "article-drafts", NULL);
      file :: ensure_dir_exists (pch);
      fname = pch;
      g_free (pch);
    }

    return fname;
  }
}

void
PostUI :: open_draft ()
{
  std::string& draft_filename (get_draft_filename ());

  GtkWidget * d = gtk_file_chooser_dialog_new (_("Open Draft Article"),
                                               GTK_WINDOW(_root),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                               NULL);
  if (!gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(d), draft_filename.c_str()))
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(d), draft_filename.c_str());
  if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
  {
    char * pch = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(d));
    draft_filename = pch;
    g_free (pch);

    if (g_file_get_contents (draft_filename.c_str(), &pch, NULL, NULL))
    {
      GMimeStream * stream = g_mime_stream_mem_new_with_buffer (pch, strlen(pch));
      GMimeParser * parser = g_mime_parser_new_with_stream (stream);
      GMimeMessage * message = g_mime_parser_construct_message (parser);
      if (message) {
        set_message (message);
        g_object_unref (G_OBJECT(message));
      }
      g_object_unref (G_OBJECT(parser));
      g_object_unref (G_OBJECT(stream));
      g_free (pch);
    }

  }
  gtk_widget_destroy (d);
}

namespace
{
  std::string get_user_agent ()
  {
    std::string s = PACKAGE_STRING;
    s += " (";
    s += VERSION_TITLE;
    s += ')';
    return s;
  }

  bool header_has_dedicated_entry (const StringView& name)
  {
    return (name == "Subject")
        || (name == "Newsgroups")
        || (name == "To")
        || (name == "From");
  }

  bool extra_header_is_editable (const StringView& name,
                                 const StringView& value)
  {
    const bool keep = !name.empty() // make sure header exists
        && (!header_has_dedicated_entry (name)) // not an extra header
        && (name != "Xref") // not editable
        && (name != "Message-ID")
        && (name != "References")
        && ((name != "User-Agent") || (value != get_user_agent()))
        && (name.strstr ("Content-") != name.str)
        && (name.strstr ("X-Draft-") != name.str);
    return keep;
  }
}

GMimeMessage*
PostUI :: new_message_from_ui (Mode mode)
{
  GMimeMessage * msg (g_mime_message_new (false));

  // headers from the ui: From
  Profile profile;
  char * pch = gtk_combo_box_get_active_text (GTK_COMBO_BOX(_from_combo));
  std::string s;
  if (pch) {
    _profiles.get_profile (pch, profile);
    profile.get_from_header (s);
    g_free (pch);
  }
  g_mime_message_set_sender (msg, s.c_str());

  // headers from the ui: Subject
  const char * cpch (gtk_entry_get_text (GTK_ENTRY(_subject_entry)));
  g_mime_message_set_subject (msg, cpch);

  // headers from the ui: To
  const StringView to (gtk_entry_get_text (GTK_ENTRY(_to_entry)));
  if (!to.empty())
    g_mime_message_add_recipients_from_string (msg, GMIME_RECIPIENT_TYPE_TO, to.str);

  // headers from the ui: Newsgroups
  const StringView groups (gtk_entry_get_text (GTK_ENTRY(_groups_entry)));
  if (!groups.empty())
    g_mime_message_set_header (msg, "Newsgroups", groups.str);

  // add the 'hidden headers'
  foreach_const (str2str_t, _hidden_headers, it)
    if ((mode==DRAFTING) || (it->first.find ("X-Draft-")!=0))
      g_mime_message_set_header (msg, it->first.c_str(), it->second.c_str());

  // build headers from the 'more headers' entry field
  std::map<std::string,std::string> headers;
  GtkTextBuffer * buf (_headers_buf);
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  pch = gtk_text_buffer_get_text (buf, &start, &end, false);
  StringView key, val, v(pch);
  v.trim ();
  while (v.pop_token (val, '\n') && val.pop_token(key,':')) {
    key.trim ();
    val.eat_chars (1);
    val.trim ();
    std::string key_str (key.to_string());
    if (extra_header_is_editable (key, val))
      g_mime_message_set_header (msg, key.to_string().c_str(),
                                      val.to_string().c_str());
  }
  g_free (pch);

  // User-Agent
  if (!g_mime_message_get_header (msg, "User-Agent"))
    g_mime_message_set_header (msg, "User-Agent", get_user_agent().c_str());

  // body & charset
  buf = _body_buf;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  pch = gtk_text_buffer_get_text (buf, &start, &end, false);
  pch = g_strstrip (pch);
  GMimeStream * stream = g_mime_stream_mem_new_with_buffer (pch, strlen(pch));
  g_free (pch);
  const std::string charset ((mode==POSTING && !_charset.empty()) ? _charset : "UTF-8");
  if (charset != "UTF-8") {
    // add a wrapper to convert from UTF-8 to $charset
    GMimeStream * tmp = g_mime_stream_filter_new_with_stream (stream);
    g_object_unref (stream);
    GMimeFilter * filter = g_mime_filter_charset_new ("UTF-8", charset.c_str());
    g_mime_stream_filter_add (GMIME_STREAM_FILTER(tmp), filter);
    g_object_unref (filter);
    stream = tmp;
  }
  GMimeDataWrapper * content_object = g_mime_data_wrapper_new_with_stream (stream, GMIME_PART_ENCODING_DEFAULT);
  g_object_unref (stream);
  GMimePart * part = g_mime_part_new ();
  pch = g_strdup_printf ("text/plain; charset=%s", charset.c_str());
  GMimeContentType * type = g_mime_content_type_new_from_string (pch);
  g_free (pch);
  g_mime_part_set_content_type (part, type); // part owns type now. type isn't refcounted.
  g_mime_part_set_content_object (part, content_object);
  g_mime_part_set_encoding (part, GMIME_PART_ENCODING_8BIT);
  g_object_unref (content_object);
  g_mime_message_set_mime_part (msg, GMIME_OBJECT(part));
  g_object_unref (part);

  return msg;
}

void
PostUI :: save_draft ()
{
  std::string& draft_filename (get_draft_filename ());

  GtkWidget * d = gtk_file_chooser_dialog_new (
    _("Save Draft Article"),
    GTK_WINDOW(_root),
    GTK_FILE_CHOOSER_ACTION_SAVE,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
    NULL);
  gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);

  if (!gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(d), draft_filename.c_str()))
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(d), draft_filename.c_str());

  if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
  {
    GMimeMessage * msg = new_message_from_ui (DRAFTING);
    char * filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(d));

    errno = 0;
    std::ofstream o (filename);
    char * pch = g_mime_message_to_string (msg);
    o << pch;
    o.close ();

    if (o.fail()) {
      GtkWidget * e = gtk_message_dialog_new (
        GTK_WINDOW(d),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_ERROR,
        GTK_BUTTONS_CLOSE,
        _("Unable to save \"%s\" %s"), filename, file::pan_strerror(errno));
      gtk_dialog_run (GTK_DIALOG(e));
      gtk_widget_destroy (e);
    }

    g_free (pch);
    g_free (filename);
    g_object_unref (msg);

    _unchanged_body = get_text (_body_buf);
  }

  gtk_widget_destroy (d);
}

namespace
{
  GtkWidget* create_body_widget (GtkTextBuffer*& buf, GtkWidget*& view, const Prefs &prefs)
  {
    view = gtk_text_view_new ();
    buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW(view));

    // force a monospace font, and size it to 80 cols x 30 rows
    const std::string str (prefs.get_string ("monospace-font", "Monospace 10"));
    PangoFontDescription *pfd (pango_font_description_from_string (str.c_str()));
    PangoContext * context = gtk_widget_create_pango_context (view);
    const int column_width (80);
    std::string line (column_width, 'A');
    pango_context_set_font_description (context, pfd);
    PangoLayout * layout = pango_layout_new (context);
    pango_layout_set_text (layout, line.c_str(), line.size());
    PangoRectangle r;
    pango_layout_get_extents (layout, &r, 0);
    gtk_widget_set_size_request (view, PANGO_PIXELS(r.width),
                                       PANGO_PIXELS(r.height*30));
    gtk_widget_modify_font (view, pfd);
    
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(view), GTK_WRAP_NONE);
    gtk_text_view_set_editable (GTK_TEXT_VIEW(view), true);
    GtkWidget * scrolled_window = gtk_scrolled_window_new (NULL, NULL);
    gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scrolled_window),
                                         GTK_SHADOW_IN);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                    GTK_POLICY_AUTOMATIC,
                                    GTK_POLICY_AUTOMATIC);
    gtk_container_add (GTK_CONTAINER (scrolled_window), view);

    // cleanup
    g_object_unref (G_OBJECT(layout));
    g_object_unref (G_OBJECT(context));
    pango_font_description_free (pfd);

    return scrolled_window;
  }
}

void
PostUI :: update_profile_combobox ()
{
  // get the old selection
  GtkComboBox * combo (GTK_COMBO_BOX (_from_combo));
  char * active_text = gtk_combo_box_get_active_text (combo);

  // if there's not already a selection,
  // pull the default for the newsgroup
  if (!active_text)
  {
    const char * text = gtk_entry_get_text (GTK_ENTRY(_groups_entry));
    StringView line(text), groupname;
    while (line.pop_token (groupname, ',')) {
      groupname.trim ();
      if (!groupname.empty()) {
        std::string profile (_group_prefs.get_string (groupname, "posting-profile", ""));
        if (!profile.empty())
          active_text = g_strdup (profile.c_str());
      }
    }
  }

  // tear out the old entries
  GtkTreeModel * model (gtk_combo_box_get_model (combo));
  for (int i(0), qty(gtk_tree_model_iter_n_children(model,0)); i<qty; ++i)
    gtk_combo_box_remove_text (combo, 0);

  // add the new entries
  typedef std::set<std::string> names_t;
  const names_t profile_names (_profiles.get_profile_names ());
  int index (0);
  int sel_index (0);
  foreach_const (names_t, profile_names, it) {
    gtk_combo_box_append_text (combo, it->c_str());
    if (active_text && (*it == active_text))
      sel_index = index;
    ++index;
  }
 
  // ensure _something_ is selected...
  gtk_combo_box_set_active (combo, sel_index);

  // cleanup
  g_free (active_text);
}


namespace
{
  void
  load_signature (const StringView& sigfile, int type, std::string& setme)
  {
    setme.clear ();

    // check for an empty string
    StringView v (sigfile);
    v.trim ();
    if (v.empty())
      return;

    char * pch = g_strndup (v.str, v.len);
    char * sig = 0;

    if (type == Profile::TEXT)
    {
      sig = g_strdup (pch);
    }
    else if (type == Profile::FILE)
    {
      GError * err = NULL;
      if (!g_file_get_contents (pch, &sig, NULL, &err))
      {
        Log::add_err_va (_("Couldn't read signature file \"%s\": %s"), pch, err->message);
        g_error_free (err);
      }
    }
    else // command
    {
      int argc = 0;
      char ** argv = 0;

      if (g_file_test (pch, G_FILE_TEST_EXISTS))
      {
        argc = 1;
        argv = g_new (char*, 2);
        argv[0] = g_strdup (pch);
        argv[1] = NULL; /* this is for g_strfreev() */
      }
      else // parse it...
      {
        GError * err = NULL;
        if (!g_shell_parse_argv (pch, &argc, &argv, &err))
        {
          Log::add_err_va (_("Couldn't parse signature command \"%s\": %s"), pch, err->message);
          g_error_free (err);
        }
      }

      /* try to execute the file... */
      if (argc>0 && argv!=NULL && argv[0]!=NULL && g_file_test (argv[0], G_FILE_TEST_IS_EXECUTABLE))
      {
        char * spawn_stdout = NULL;
        char * spawn_stderr = NULL;
        int exit_status = 0;

        if (g_spawn_sync (NULL, argv, NULL, GSpawnFlags(0), NULL, NULL, &spawn_stdout, &spawn_stderr, &exit_status, NULL))
          sig = g_strdup (spawn_stdout);
        if (spawn_stderr && *spawn_stderr)
          Log::add_err (spawn_stderr);

        g_free (spawn_stderr);
        g_free (spawn_stdout);
      }

      g_strfreev (argv);
    }

    /* Convert signature to UTF-8. Since the signature is a local file,
     * we assume the contents is in the user's locale's charset.
     * If we can't convert, clear the signature. Otherwise, we'd add an
     * charset-encoded sig (say 'iso-8859-1') to the body (in UTF-8),
     * which could result in a blank message in the composer window. */
    if (sig!=NULL)
    {
      const std::string s (content_to_utf8 (sig));
      g_free (sig);
      sig = g_strndup (s.c_str(), s.size());
    }
    else
    {
      Log::add_err (_("Couldn't convert signature to UTF-8."));
      g_free (sig);
      sig = NULL;
    }

    if (sig)
      setme = sig;

    /* cleanup */
    g_free (pch);
    g_free (sig);
  }
}

void
PostUI :: apply_profile ()
{
  apply_profile_to_body ();
  apply_profile_to_headers ();
}

namespace
{
  void replace (std::string        & in,
                const std::string  & from,
                const std::string  & to)
  {
    std::string out;
    std::string::size_type b(0), e(0);
    for (;;) {
      e = in.find (from, b);
      if (e == std::string::npos) {
        out.append (in, b, std::string::npos);
        break;
      } else {
        out.append (in, b, e-b);
        out.append (to);
        b = e + from.size();
      }
    }
    in = out;
  }

  bool do_attribution_substitutions (const StringView & mid,
                                     const StringView & date,
                                     const StringView & from,
                                     std::string& attrib)
  {
    if (mid.empty() && date.empty() && from.empty()) // not a follow-up; attribution not needed
      return false;

    const StringView brief = GNKSA :: get_short_author_name (from);
    replace (attrib, "%i", mid);
    replace (attrib, "%d", date);
    replace (attrib, "%a", from);
    replace (attrib, "%n", brief.to_string());
    return true;
  }
}

void
PostUI :: apply_profile_to_body ()
{
  // get the selected profile
  Profile profile;
  char * pch = gtk_combo_box_get_active_text (GTK_COMBO_BOX(_from_combo));
  if (pch) {
    _profiles.get_profile (pch, profile);
    g_free (pch);
  }
  std::string attribution = profile.attribution;
  if (do_attribution_substitutions (_hidden_headers["X-Draft-Attribution-Id"],
                                    _hidden_headers["X-Draft-Attribution-Date"],
                                    _hidden_headers["X-Draft-Attribution-Author"],
                                    attribution))
    attribution = _tm.fill (attribution);
  else
    attribution.clear ();

  // get current the body
  GtkTextBuffer * buf (_body_buf);
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  pch = gtk_text_buffer_get_text (buf, &start, &end, false);
  std::string body;
  if (pch)
    body = pch;
  g_free (pch);

  // replace the attribution
  const std::string old_attribution (_hidden_headers["X-Draft-Attribution"]);
  if (!attribution.empty())
  {
    // scrub the attribution for UTF8 cleanness
    attribution = header_to_utf8 (attribution);
 
    std::string::size_type pos = body.find (old_attribution);
    if (!old_attribution.empty() && (pos != std::string::npos))
      body.replace (pos, old_attribution.size(), attribution);
    else if (!attribution.empty())
      body = attribution + "\n\n" + body;
    _hidden_headers["X-Draft-Attribution"] = attribution;
  }

  // remove the last signature
  int index;
  if (!GNKSA :: find_signature_delimiter (body, index))
    index = body.size();
  StringView v (body.c_str(), index);
  v.trim ();
  body.assign (v.str, v.len);
  if (!body.empty())
    body += "\n\n";
  const int insert_pos = body.size();

  // insert the new signature
  std::string sig;
  load_signature (profile.signature_file, profile.sig_type, sig);
  if (!sig.empty()) {
    int ignored;
    if (GNKSA::find_signature_delimiter (sig, ignored) == GNKSA::SIG_NONE)
      body += "\n\n-- \n";
    body += sig;
  }

  gtk_text_buffer_set_text (buf, body.c_str(), body.size()); // FIXME: wrap

  // set & scroll-to the insert point
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_offset (buf, &iter, insert_pos);
  gtk_text_buffer_move_mark_by_name (buf, "insert", &iter);
  gtk_text_buffer_move_mark_by_name (buf, "selection_bound", &iter);
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW(_body_view),
                                gtk_text_buffer_get_mark(buf, "insert"),
                                0.0, false, 0.0, 0.0);
}

void
PostUI :: apply_profile_to_headers ()
{
  // get the current `extra headers'
  GtkTextBuffer * buf (_headers_buf);
  Profile::headers_t headers;
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  char * text (gtk_text_buffer_get_text (buf,  &start, &end, false));
  StringView lines(text), line;
  while (lines.pop_token (line, '\n')) {
    StringView key, value(line);
    if (value.pop_token (key, ':')) {
      key.trim ();
      value.trim ();
      if (!key.empty() && !value.empty())
        headers[key] = value;
    }
  }
  g_free (text);

  // remove from current all the headers from the old profile.
  foreach_const (str2str_t, _profile_headers, it)
    if (headers.count(it->first) && headers[it->first]==it->second)
      headers.erase (it->first);

  // get the new profile
  Profile profile;
  char * pch = gtk_combo_box_get_active_text (GTK_COMBO_BOX(_from_combo));
  if (pch) {
    _profiles.get_profile (pch, profile);
    g_free (pch);
  }

  // add all the headers from the new profile.
  _profile_headers = profile.headers;
  foreach_const (Profile::headers_t, profile.headers, it)
    headers[it->first] = it->second;

  // rewrite the extra headers pane
  std::string s;
  foreach_const (Profile::headers_t, headers, it)
    s += it->first + ": " + it->second + "\n";
  gtk_text_buffer_set_text (buf, s.c_str(), s.size());
}

namespace
{
  void delete_post_ui (gpointer p)
  {
    delete static_cast<PostUI*>(p);
  }

  void on_from_combo_changed (GtkComboBox *widget, gpointer user_data)
  {
    static_cast<PostUI*>(user_data)->apply_profile ();
  }

  typedef std::map <std::string, std::string> str2str_t;

  struct SetMessageForeachHeaderData
  {
    str2str_t hidden_headers;
    std::string visible_headers;
  };

  void set_message_foreach_header_func (const char * name, const char * value, gpointer data_gpointer)
  {
    struct SetMessageForeachHeaderData * data (static_cast<SetMessageForeachHeaderData*>(data_gpointer));

    if (header_has_dedicated_entry (name))
    {
      // it's not an extra header
    }
    else if (extra_header_is_editable (name, value))
    {
      // it's a visible extra header.
      std::string& str (data->visible_headers);
      str += std::string(name) + ": " + value + "\n";
    }
    else
    {
      // it's a hidden extra header -- X-Draft-* headers, etc.
      data->hidden_headers[name] = value;
    }
  }
}

void
PostUI :: set_message (GMimeMessage * message)
{
  // update our message header
  if (message)
    g_object_ref (G_OBJECT(message));
  if (_message)
    g_object_unref (G_OBJECT(_message));
  _message = message;

  // update subject, newsgroups, to fields
  const char * cpch = g_mime_message_get_subject (message);
  gtk_entry_set_text (GTK_ENTRY(_subject_entry), cpch ? cpch : "");
  cpch = g_mime_message_get_header (message, "Newsgroups");
  gtk_entry_set_text (GTK_ENTRY(_groups_entry), cpch ? cpch : "");
  const InternetAddressList * addresses = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_TO);
  char * pch  = internet_address_list_to_string (addresses, true);
  gtk_entry_set_text (GTK_ENTRY(_to_entry), pch ? pch : "");
  g_free (pch);

  // update 'other headers'
  SetMessageForeachHeaderData data;
  if (message->mime_part && g_mime_header_has_raw (message->mime_part->headers))
    g_mime_header_foreach (message->mime_part->headers, set_message_foreach_header_func, &data);
  g_mime_header_foreach (GMIME_OBJECT(message)->headers, set_message_foreach_header_func, &data);
  gtk_text_buffer_set_text (_headers_buf, data.visible_headers.c_str(), -1);
  _hidden_headers = data.hidden_headers;

  // update body
  int ignored;
  char * tmp = g_mime_message_get_body (message, true, &ignored);
  if (tmp) {
    std::string s = tmp;
    s += "\n\n";
    gtk_text_buffer_set_text (_body_buf, s.c_str(), s.size());
  }
  g_free (tmp);

  // apply the profiles
  update_profile_combobox ();
  apply_profile ();
}

/***
****
***/

gboolean
PostUI :: group_entry_changed_idle (gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));
  std::string charset;

  // find the first posting charset in the newsgroups in _groups_entry.
  const char * text = gtk_entry_get_text (GTK_ENTRY(ui->_groups_entry));
  StringView line(text), groupname;
  while (line.pop_token (groupname, ',')) {
    groupname.trim ();
    if (groupname.empty())
      continue;
    charset = ui->_group_prefs.get_string (groupname, "character-encoding", "UTF-8");
    if (!charset.empty())
      break;
  }

  // if user hasn't specified a charset by hand,
  // use this one as the `default' charset for the groups being posted to.
  if (!charset.empty())
    ui->set_charset (charset);
  ui->_group_entry_changed_idle_tag = 0;
  return false;
}

void
PostUI :: group_entry_changed_cb (GtkEditable * w, gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));
  unsigned int& tag (ui->_group_entry_changed_idle_tag);
  if (!tag)
    tag = g_timeout_add (2000, group_entry_changed_idle, ui);
}

/***
****
***/

PostUI :: ~PostUI ()
{
  if (_group_entry_changed_idle_tag)
    g_source_remove (_group_entry_changed_idle_tag);

  g_object_unref (G_OBJECT(_message));
}

PostUI :: PostUI (GtkWindow    * parent,
                  Data         & data,
                  Queue        & queue,
                  GroupServer  & gs,
                  Profiles     & profiles,
                  GMimeMessage * message,
                  Prefs        & prefs,
                  GroupPrefs   & group_prefs):
  _data (data),
  _queue (queue),
  _gs (gs),
  _profiles (profiles),
  _prefs (prefs),
  _group_prefs (group_prefs),
  _root (0),
  _from_combo (0),
  _subject_entry (0),
  _groups_entry (0),
  _to_entry (0),
  _body_view (0),
  _body_buf (0),
  _message (0),
  _charset (DEFAULT_CHARSET),
  _group_entry_changed_id (0),
  _group_entry_changed_idle_tag (0)
{
  g_assert (profiles.has_profiles());
  g_return_if_fail (message != 0);

  // create the window
  _root = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (_root, "delete-event", G_CALLBACK(delete_event_cb), this);
  gtk_window_set_role (GTK_WINDOW(_root), "pan-post-window");
  gtk_window_set_title (GTK_WINDOW(_root), _("Post Article"));
  GdkPixbuf * pixbuf = gdk_pixbuf_new_from_inline (-1, icon_pan, FALSE, 0);
  gtk_window_set_icon (GTK_WINDOW(_root), pixbuf); 
  g_object_unref (G_OBJECT(pixbuf));
  g_object_set_data_full (G_OBJECT(_root), "post-ui", this, delete_post_ui);
  if (parent) {
    gtk_window_set_transient_for (GTK_WINDOW(_root), parent);
    gtk_window_set_position (GTK_WINDOW(_root), GTK_WIN_POS_CENTER_ON_PARENT);
  }

  GtkTooltips * tips = gtk_tooltips_new ();
  g_object_ref_sink_pan (G_OBJECT(tips));
  g_object_weak_ref (G_OBJECT(_root), (GWeakNotify)g_object_unref, tips);

  // populate the window
  GtkWidget * vbox = gtk_vbox_new (false, PAD_SMALL);
  GtkWidget * menu_vbox = gtk_vbox_new (false, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX(vbox), menu_vbox, false, false, 0);
  add_actions (menu_vbox);
  gtk_window_add_accel_group (GTK_WINDOW(_root), gtk_ui_manager_get_accel_group (_uim));
  gtk_widget_show_all (vbox);
  gtk_container_add (GTK_CONTAINER(_root), vbox);

  // add the headers table
  const GtkAttachOptions fill ((GtkAttachOptions)(GTK_FILL|GTK_EXPAND|GTK_SHRINK));
  const GtkAttachOptions nofill ((GtkAttachOptions)0);
  char buf[512];
  int row = 0;
  GtkWidget *l, *t, *w;
  t = HIG :: workarea_create ();
  w = gtk_alignment_new (0.0f, 0.0f, 0.0f, 0.0f);
  gtk_widget_set_size_request (w, 12u, 0u);
  gtk_table_attach (GTK_TABLE(t), w, 2, 3, row, row+4, nofill, nofill, 0, 0);
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("F_rom"));
  l = HIG :: workarea_add_label (t, row, buf);
  w = _from_combo = gtk_combo_box_new_text ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  g_signal_connect (w, "changed", G_CALLBACK(on_from_combo_changed), this);
  gtk_table_attach (GTK_TABLE(t), w, 3, 4, row, row+1, fill, nofill, 0, 0);
  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Subject"));
  l = HIG :: workarea_add_label (t, row, buf);
  w = _subject_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 3, 4, row, row+1, fill, nofill, 0, 0);
  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Newsgroups"));
  l = HIG :: workarea_add_label (t, row, buf);
  w = _groups_entry = gtk_entry_new ();
  _group_entry_changed_id = g_signal_connect (w, "changed", G_CALLBACK(group_entry_changed_cb), this);
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 3, 4, row, row+1, fill, nofill, 0, 0);
  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Mail To"));
  l = HIG :: workarea_add_label (t, row, buf);
  w = _to_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 3, 4, row, row+1, fill, nofill, 0, 0);
  ++row;
  GtkWidget * frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  GtkWidget * v = gtk_text_view_new ();
  gtk_text_view_set_accepts_tab (GTK_TEXT_VIEW(v), false);
  gtk_tooltips_set_tip (tips, v, _("One header per line, in the form HeaderName: Value"), 0);
  _headers_buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW(v));
  gtk_container_add (GTK_CONTAINER(frame), v);
  g_snprintf (buf, sizeof(buf), "<b>%s</b>", _("_More Headers"));
  w = GTK_WIDGET (g_object_new (GTK_TYPE_EXPANDER,
    "use-markup", TRUE,
    "use-underline", TRUE,
    "label", buf, NULL));
  gtk_container_add (GTK_CONTAINER(w), frame);
  gtk_table_attach (GTK_TABLE(t), w, 1, 4, row, row+1, fill, nofill, 0, 0);
  ++row;
  gtk_box_pack_start (GTK_BOX(vbox), t, false, false, 0);

  // add the body text widget
  w = create_body_widget (_body_buf, _body_view, prefs);
  set_spellcheck_enabled (prefs.get_flag ("spellcheck-enabled", DEFAULT_SPELLCHECK_FLAG));
  g_signal_connect (_body_buf, "insert-text", G_CALLBACK(text_was_inserted_cb), &_tm);
  gtk_box_pack_start (GTK_BOX(vbox), w, true, true, 0);

  set_message (message);

  _unchanged_body = get_text (_body_buf);

  // set focus to the first non-populated widget
  GtkWidget * grab (0);
  if (!grab) {
    const char * cpch (gtk_entry_get_text (GTK_ENTRY(_subject_entry)));
    if (!cpch || !*cpch)
      grab = _subject_entry;
  }
  if (!grab) {
    const StringView one (gtk_entry_get_text (GTK_ENTRY(_groups_entry)));
    const StringView two (gtk_entry_get_text (GTK_ENTRY(_to_entry)));
    if (one.empty() && two.empty())
      grab = _groups_entry;
  }
  if (!grab)
    grab = _body_view;
  gtk_widget_grab_focus (grab);
}

PostUI*
PostUI :: create_window (GtkWindow    * parent,
                         Data         & data,
                         Queue        & queue,
                         GroupServer  & gs,
                         Profiles     & profiles,
                         GMimeMessage * message,
                         Prefs        & prefs,
                         GroupPrefs   & group_prefs)
{
  // can't post without a profile...
  if (!profiles.has_profiles())
  {
    Profile profile;
    profile.username = g_get_real_name ();
    /* xgettext: no-c-format */
    profile.attribution = _("On %d, %n wrote:");
    ProfileDialog d (data, g_get_real_name(), profile, GTK_WINDOW(parent));
    const bool got_profile (ProfileDialog :: run_until_valid_or_cancel (d));
    if (got_profile) {
      std::string name;
      d.get_profile (name, profile);
      profiles.add_profile (name, profile);
    }
    gtk_widget_destroy (d.root());
    if (!got_profile)
      return 0;
  }

  return new PostUI (parent, data, queue, gs, profiles, message, prefs, group_prefs);
}
