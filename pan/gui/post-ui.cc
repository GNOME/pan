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
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/usenet-utils/message-check.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/data/data.h>
#include <pan/tasks/task-post.h>
#include "e-charset-dialog.h"
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

#define USER_AGENT_PREFS_KEY "add-user-agent-header-when-posting"
#define MESSAGE_ID_PREFS_KEY "add-message-id-header-when-posting"
#define USER_AGENT_EXTRA_PREFS_KEY "user-agent-extra-info"

namespace
{
  bool remember_charsets (true);

  void on_remember_charset_toggled (GtkToggleAction * toggle, gpointer)
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

/**
 * get current the body.
 * since Pan posts WYSIWYG, pull from the view's lines
 * rather than just using text_buffer_get_text(start,end)
 */
std::string
PostUI :: get_body () const
{
  std::string body;
  GtkTextBuffer * buf (_body_buf);
  GtkTextView * view (GTK_TEXT_VIEW(_body_view));
  const bool wrap (_prefs.get_flag ("compose-wrap-enabled", false));
  
  // walk through all the complete lines...
  GtkTextIter body_start, body_end, line_start, line_end;
  gtk_text_buffer_get_bounds (buf, &body_start, &body_end);
  line_start = line_end = body_start;
  while ((gtk_text_view_forward_display_line (view, &line_end))) {
    char * line = gtk_text_buffer_get_text (buf, &line_start, &line_end, false);
    body += line;
    g_free (line);
    if (wrap && *body.rbegin() != '\n')
      body += '\n';
    line_start = line_end;
  }

  // and maybe the last line doesn't have a linefeed yet...
  char * last_line = gtk_text_buffer_get_text (buf, &line_start, &body_end, false);
  if (last_line && *last_line)
    body += last_line;
  g_free (last_line);

  return body;
}

void
PostUI :: set_always_run_editor (bool run)
{
  _prefs.set_flag ("always-run-editor", run);
}

void
PostUI :: set_wrap_mode (bool wrap)
{
  _prefs.set_flag ("compose-wrap-enabled", wrap);

  if (_body_buf) {
    const std::string s (get_body());
    gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(_body_view),
                                 wrap ? GTK_WRAP_WORD : GTK_WRAP_NONE);
    gtk_text_buffer_set_text (_body_buf, s.c_str(), s.size());
  }
}

/***
****  Menu and Toolbar
***/

namespace
{
  GtkWidget* get_focus (gpointer p) { return gtk_window_get_focus(GTK_WINDOW(static_cast<PostUI*>(p)->root())); }

  void do_cut      (GtkAction*, gpointer p) { g_signal_emit_by_name (get_focus(p), "cut_clipboard"); }
  void do_copy     (GtkAction*, gpointer p) { g_signal_emit_by_name (get_focus(p), "copy_clipboard"); }
  void do_paste    (GtkAction*, gpointer p) { g_signal_emit_by_name (get_focus(p), "paste_clipboard"); }
  void do_rot13    (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->rot13_selection(); }
  void do_edit     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->spawn_editor (); }
  void do_profiles (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->manage_profiles (); }
  void do_send     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->send_now (); }
  void do_save     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->save_draft (); }
  void do_open     (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->open_draft (); }
  void do_charset  (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->prompt_for_charset (); }
  void do_close    (GtkAction*, gpointer p) { static_cast<PostUI*>(p)->close_window (); }
  void do_wrap     (GtkToggleAction * w, gpointer p) { static_cast<PostUI*>(p)->set_wrap_mode (gtk_toggle_action_get_active (w)); }
  void do_edit2    (GtkToggleAction * w, gpointer p) { static_cast<PostUI*>(p)->set_always_run_editor (gtk_toggle_action_get_active (w)); }

  GtkActionEntry entries[] =
  {
    { "file-menu", 0, N_("_File"), 0, 0, 0 },
    { "edit-menu", 0, N_("_Edit"), 0, 0, 0 },
    { "profile-menu", 0, N_("_Profile"), 0, 0, 0 },
    { "editors-menu", 0, N_("Set Editor"), 0, 0, 0 },
    { "post-toolbar", 0, "post", 0, 0, 0 },
    { "post-article", GTK_STOCK_EXECUTE, N_("_Send Article"), "<control>Return", N_("Send Article Now"), G_CALLBACK(do_send) },
    { "set-charset", 0, N_("Set Character _Encoding..."), 0, 0, G_CALLBACK(do_charset) },
    { "save-draft", GTK_STOCK_SAVE, N_("Sa_ve Draft"), "<control>s", N_("Save as a Draft for Future Posting"), G_CALLBACK(do_save) },
    { "open-draft", GTK_STOCK_OPEN, N_("_Open Draft..."), "<control>o", N_("Open an Article Draft"), G_CALLBACK(do_open) },
    { "close", GTK_STOCK_CLOSE, 0, 0, 0, G_CALLBACK(do_close) },
    { "cut", GTK_STOCK_CUT, 0, 0, 0, G_CALLBACK(do_cut) },
    { "copy", GTK_STOCK_COPY, 0, 0, 0, G_CALLBACK(do_copy) },
    { "paste", GTK_STOCK_PASTE, 0, 0, 0, G_CALLBACK(do_paste) },
    { "rot13", GTK_STOCK_REFRESH, N_("_Rot13"), 0, N_("Rot13 Selected Text"), G_CALLBACK(do_rot13) },
    { "run-editor", GTK_STOCK_JUMP_TO, N_("Run _Editor"), "<control>e", N_("Run Editor"), G_CALLBACK(do_edit) },
    { "manage-profiles", GTK_STOCK_EDIT, N_("Edit P_osting Profiles"), 0, 0, G_CALLBACK(do_profiles) }
  };

  GtkToggleActionEntry toggle_entries[] =
  {
    { "wrap", GTK_STOCK_JUSTIFY_FILL, N_("_Wrap Text"), 0, N_("Wrap Text"), G_CALLBACK(do_wrap), true },
    { "always-run-editor", 0, N_("Always Run Editor"), 0, 0, G_CALLBACK(do_edit2), false },
    { "remember-charset", 0, N_("Remember Character Encoding for this Group"), 0, 0, G_CALLBACK(on_remember_charset_toggled), true },
    { "spellcheck", 0, N_("Check _Spelling"), 0, 0, G_CALLBACK(on_spellcheck_toggled), true }
  };

  void add_widget (GtkUIManager*, GtkWidget* widget, gpointer vbox)
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
}

#define DEFAULT_CHARSET  "UTF-8"

void
PostUI :: prompt_for_charset ()
{
  if (_charset.empty())
      _charset = DEFAULT_CHARSET;

  char * tmp = e_charset_dialog (_("Character Encoding"),
                                 _("New Article's Encoding:"),
                                 _charset.c_str(),
                                 GTK_WINDOW(root()));
  set_charset (tmp);
  free (tmp);
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
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (_agroup, "always-run-editor")),
                                _prefs.get_flag ("always-run-editor", false));
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (_agroup, "spellcheck")),
                                _prefs.get_flag ("spellcheck-enabled", DEFAULT_SPELLCHECK_FLAG));
  gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (gtk_action_group_get_action (_agroup, "wrap")),
                                _prefs.get_flag ("compose-wrap-enabled", true));
  gtk_ui_manager_insert_action_group (_uim, _agroup, 0);
}

void
PostUI :: set_charset (const StringView& charset)
{
  _charset = charset;
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

namespace
{
  gboolean delete_event_cb (GtkWidget*, GdkEvent*, gpointer user_data)
  {
    static_cast<PostUI*>(user_data)->close_window ();
    return true; // don't invoke the default handler that destroys the widget
  }
}

void
PostUI :: close_window ()
{
  bool destroy_flag (false);;

  if (get_body() == _unchanged_body)
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
  const std::string body (get_body ());
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
    _unchanged_body = get_body ();
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
      for (const char *pch="$-_.+!*'(),"; *pch; ++pch) keep.insert(*pch); // okay in the right context
      for (const char *pch="$&+,/:;=?@"; *pch; ++pch) keep.erase(*pch); // reserved
      for (const char *pch="()"; *pch; ++pch) keep.erase(*pch); // gives thunderbird problems?
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
  const Profile profile (get_current_profile ());
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

  const std::string body (get_body ());

  if (ok) {
    if (fwrite (body.c_str(), sizeof(char), body.size(), fp) != body.size()) {
      ok = false;
      Log::add_err_va (_("Error writing article to temporary file: %s"), g_strerror(errno));
    }
  }

  if (fp != NULL) {
    fclose (fp);
    fp = NULL;
  }

  // parse the command line
  int argc (0);
  char ** argv (0);
  if (ok) {
    std::set<std::string> editors;
    URL :: get_default_editors (editors);
    const std::string editor (_prefs.get_string ("editor", *editors.begin()));
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
  std::string txt;
  if (ok && file :: get_text_file_contents (fname, txt)) {
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds (buf, &start, &end);
    gtk_text_buffer_delete (buf, &start, &end);
    gtk_text_buffer_insert (buf, &start, txt.c_str(), txt.size());
  }

  // cleanup
  ::remove (fname);
  g_free (fname);
  g_strfreev (argv);

  gtk_window_present (GTK_WINDOW(root()));
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
  GtkWidget * d = gtk_file_chooser_dialog_new (_("Open Draft Article"),
                                               GTK_WINDOW(_root),
                                               GTK_FILE_CHOOSER_ACTION_OPEN,
                                               GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
                                               GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
                                               NULL);

  std::string& draft_filename (get_draft_filename ());
  if (g_file_test (draft_filename.c_str(), G_FILE_TEST_IS_DIR))
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(d), draft_filename.c_str());
  else
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(d), draft_filename.c_str());

  if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
  {
    char * pch = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(d));
    draft_filename = pch;
    g_free (pch);

    std::string txt;
    if (file :: get_text_file_contents (draft_filename, txt))
    {
      GMimeStream * stream = g_mime_stream_mem_new_with_buffer (txt.c_str(), txt.size());
      GMimeParser * parser = g_mime_parser_new_with_stream (stream);
      GMimeMessage * message = g_mime_parser_construct_message (parser);
      if (message) {
        set_message (message);
        g_object_unref (G_OBJECT(message));
      }
      g_object_unref (G_OBJECT(parser));
      g_object_unref (G_OBJECT(stream));
    }

  }
  gtk_widget_destroy (d);
}

namespace
{
  bool ua_extra=false;

  const char * get_user_agent ()
  {
    if (ua_extra)
      return "Pan/" PACKAGE_VERSION " (" VERSION_TITLE "; " GIT_REV "; " PLATFORM_INFO ")";
    else
      return "Pan/" PACKAGE_VERSION " (" VERSION_TITLE "; " GIT_REV ")";
  }

  bool header_has_dedicated_entry (const StringView& name)
  {
    return (name == "Subject")
        || (name == "Newsgroups")
        || (name == "To")
        || (name == "From")
        || (name == "Followup-To")
        || (name == "Reply-To");
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

  /**
   * Works around a GMime bug that uses `Message-Id' rather than `Message-ID'
   */
  void pan_g_mime_message_set_message_id (GMimeMessage *msg, const char *mid)
  {
    g_mime_message_add_header (msg, "Message-ID", mid);
    char * bracketed = g_strdup_printf ("<%s>", mid);
    g_mime_header_set (GMIME_OBJECT(msg)->headers, "Message-ID", bracketed);
    g_free (bracketed);
  }
}

GMimeMessage*
PostUI :: new_message_from_ui (Mode mode)
{
  GMimeMessage * msg (g_mime_message_new (false));

  // headers from the ui: From
  const Profile profile (get_current_profile ());
  std::string s;
  profile.get_from_header (s);
  g_mime_message_set_sender (msg, s.c_str());

  // headers from the ui: Subject
  const char * cpch (gtk_entry_get_text (GTK_ENTRY(_subject_entry)));
  g_mime_message_set_subject (msg, cpch);

  // headers from the ui: To
  const StringView to (gtk_entry_get_text (GTK_ENTRY(_to_entry)));
  if (!to.empty())
    g_mime_message_add_recipients_from_string (msg, (char*)GMIME_RECIPIENT_TYPE_TO, to.str);

  // headers from the ui: Newsgroups
  const StringView groups (gtk_entry_get_text (GTK_ENTRY(_groups_entry)));
  if (!groups.empty())
    g_mime_message_set_header (msg, "Newsgroups", groups.str);

  // headers from the ui: Followup-To
  const StringView followupto (gtk_entry_get_text (GTK_ENTRY(_followupto_entry)));
  if (!followupto.empty())
    g_mime_message_set_header (msg, "Followup-To", followupto.str);

  // headers from the ui: Reply-To
  const StringView replyto (gtk_entry_get_text (GTK_ENTRY(_replyto_entry)));
  if (!replyto.empty())
    g_mime_message_set_header (msg, "Reply-To", replyto.str);

  // add the 'hidden headers'
  foreach_const (str2str_t, _hidden_headers, it)
    if ((mode==DRAFTING) || (it->first.find ("X-Draft-")!=0))
      g_mime_message_set_header (msg, it->first.c_str(), it->second.c_str());

  // build headers from the 'more headers' entry field
  std::map<std::string,std::string> headers;
  GtkTextBuffer * buf (_headers_buf);
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (buf, &start, &end);
  char * pch = gtk_text_buffer_get_text (buf, &start, &end, false);
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
  if (mode==POSTING && _prefs.get_flag (USER_AGENT_PREFS_KEY, true))
    g_mime_message_set_header (msg, "User-Agent", get_user_agent());

  // Message-ID
  if (mode==POSTING && _prefs.get_flag (MESSAGE_ID_PREFS_KEY, false)) {
    const std::string message_id = !profile.fqdn.empty()
      ? GNKSA::generate_message_id (profile.fqdn)
      : GNKSA::generate_message_id_from_email_address (profile.address);
    pan_g_mime_message_set_message_id (msg, message_id.c_str());
  }

  // body & charset
  std::string body (get_body());
  GMimeStream * stream = g_mime_stream_mem_new_with_buffer (body.c_str(), body.size());
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
  GtkWidget * d = gtk_file_chooser_dialog_new (
    _("Save Draft Article"),
    GTK_WINDOW(_root),
    GTK_FILE_CHOOSER_ACTION_SAVE,
    GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
    GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT,
    NULL);
  gtk_dialog_set_default_response (GTK_DIALOG(d), GTK_RESPONSE_ACCEPT);

  std::string& draft_filename (get_draft_filename ());
  if (g_file_test (draft_filename.c_str(), G_FILE_TEST_IS_DIR))
    gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER(d), draft_filename.c_str());
  else
    gtk_file_chooser_set_filename (GTK_FILE_CHOOSER(d), draft_filename.c_str());

  if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_ACCEPT)
  {
    GMimeMessage * msg = new_message_from_ui (DRAFTING);
    char * filename = gtk_file_chooser_get_filename (GTK_FILE_CHOOSER(d));
    draft_filename = filename;

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

    _unchanged_body = get_body ();
  }

  gtk_widget_destroy (d);
}

void
PostUI :: body_widget_resized_cb (GtkWidget      * w,
                                  GtkAllocation  * allocation,
                                  gpointer         self)
{
  gtk_text_view_set_right_margin (
    GTK_TEXT_VIEW(w),
    allocation->width - static_cast<PostUI*>(self)->_wrap_pixels);
}

GtkWidget*
PostUI :: create_body_widget (GtkTextBuffer*& buf, GtkWidget*& view, const Prefs &prefs)
{
  const int WRAP_COLS = 75;
  const int VIEW_COLS = 80;

  view = gtk_text_view_new ();
  buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW(view));

  // always use a monospace font in the compose window
  const std::string str (prefs.get_string ("monospace-font", "Monospace 10"));
  PangoFontDescription *pfd (pango_font_description_from_string (str.c_str()));
  gtk_widget_modify_font (view, pfd);

  // figure out how wide the text is before the wrap point
  PangoContext * context = gtk_widget_create_pango_context (view);
  pango_context_set_font_description (context, pfd);
  PangoLayout * layout = pango_layout_new (context);
  std::string s (WRAP_COLS, 'A');
  pango_layout_set_text (layout, s.c_str(), s.size());
  PangoRectangle r;
  pango_layout_get_extents (layout, &r, 0);
  _wrap_pixels = PANGO_PIXELS(r.width);

  // figure out how wide we want to make the window
  s.assign (VIEW_COLS, 'A');
  pango_layout_set_text (layout, s.c_str(), s.size());
  pango_layout_get_extents (layout, &r, 0);
  gtk_widget_set_size_request (view, PANGO_PIXELS(r.width), -1 );
 
  // set the rest of the text view's policy 
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(view), GTK_WRAP_WORD);
  gtk_text_view_set_editable (GTK_TEXT_VIEW(view), true);
  GtkWidget * scrolled_window = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scrolled_window),
                                       GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER (scrolled_window), view);
  g_signal_connect (view, "size-allocate", G_CALLBACK(body_widget_resized_cb), this);

  // cleanup
  g_object_unref (G_OBJECT(layout));
  g_object_unref (G_OBJECT(context));
  pango_font_description_free (pfd);

  return scrolled_window;
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
    std::string sig;

    if (type == Profile::TEXT)
    {
      sig = pch;
    }
    else if (type == Profile::FILE)
    {
      file :: get_text_file_contents (pch, sig);
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
          sig = spawn_stdout;
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
    if (!sig.empty())
      sig = content_to_utf8 (sig);
    else
      Log::add_err (_("Couldn't convert signature to UTF-8."));

    if (!sig.empty())
      setme = sig;

    /* cleanup */
    g_free (pch);
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

Profile
PostUI :: get_current_profile ()
{
  Profile profile;
  char * pch = gtk_combo_box_get_active_text (GTK_COMBO_BOX(_from_combo));
  if (pch) {
    _profiles.get_profile (pch, profile);
    g_free (pch);
  }
  return profile;
}

void
PostUI :: apply_profile_to_body ()
{
  // get the selected profile
  const Profile profile (get_current_profile ());
  std::string attribution = profile.attribution;
  if (do_attribution_substitutions (_hidden_headers["X-Draft-Attribution-Id"],
                                    _hidden_headers["X-Draft-Attribution-Date"],
                                    _hidden_headers["X-Draft-Attribution-Author"],
                                    attribution))
    attribution = _tm.fill (attribution);
  else
    attribution.clear ();

  std::string body = get_body ();

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
  std::string::size_type pos = body.rfind (_current_signature);
  if (pos != body.npos) {
    body.resize (pos);
    StringView v (body);
    v.rtrim ();
    body.assign (v.str, v.len);
  }

  // get the new signature
  std::string sig;
  if (profile.use_sigfile) {
    load_signature (profile.signature_file, profile.sig_type, sig);
    int ignored;
    if (GNKSA::find_signature_delimiter (sig, ignored) == GNKSA::SIG_NONE)
      sig = std::string("\n\n-- \n") + sig;
  }
  _current_signature = sig;

  // add the new signature, and empty space between the
  // body (if present), the insert point, and the new signature
  int insert_pos;
  if (body.empty() && sig.empty()) {
    insert_pos = 0;
  } else if (body.empty()) {
    insert_pos = 0;
    body = "\n\n";
    body += sig;
  } else if (sig.empty()) {
    body += "\n\n";
    insert_pos = body.size();
  } else {
    body += "\n\n";
    insert_pos = body.size();
    body += "\n\n";
    body += sig;
  }

  GtkTextBuffer * buf (_body_buf);
  gtk_text_buffer_set_text (buf, body.c_str(), body.size());

  // set & scroll-to the insert point
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_offset (buf, &iter, insert_pos);
  gtk_text_buffer_move_mark_by_name (buf, "insert", &iter);
  gtk_text_buffer_move_mark_by_name (buf, "selection_bound", &iter);
  gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW(_body_view),
                                gtk_text_buffer_get_mark(buf, "insert"),
                                0.0, true, 0.0, 0.5);
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
  const Profile profile (get_current_profile ());

  // add all the headers from the new profile.
  _profile_headers = profile.headers;
  foreach_const (Profile::headers_t, profile.headers, it)
    headers[it->first] = it->second;

  // if user has custom reply-to header, handle that here.
  Profile::headers_t::iterator p = headers.find ("Reply-To");
  if (p != headers.end()) {
    gtk_entry_set_text (GTK_ENTRY(_replyto_entry), p->second.c_str());
    headers.erase (p);
  }

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

  void on_from_combo_changed (GtkComboBox*, gpointer user_data)
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

std::string
PostUI :: utf8ize (const StringView& in) const
{
  const char * local_charset = 0;
  g_get_charset (&local_charset);
  return content_to_utf8 (in, _charset.c_str(), local_charset);
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
  std::string s = utf8ize (g_mime_message_get_subject (message));
  gtk_entry_set_text (GTK_ENTRY(_subject_entry), s.c_str());

  s = utf8ize (g_mime_message_get_header (message, "Newsgroups"));
  gtk_entry_set_text (GTK_ENTRY(_groups_entry), s.c_str());

  s = utf8ize (g_mime_message_get_header (message, "Followup-To"));
  gtk_entry_set_text (GTK_ENTRY(_followupto_entry), s.c_str());

  s = utf8ize (g_mime_message_get_header (message, "Reply-To"));
  gtk_entry_set_text (GTK_ENTRY(_replyto_entry), s.c_str());

  const InternetAddressList * addresses = g_mime_message_get_recipients (message, GMIME_RECIPIENT_TYPE_TO);
  char * pch  = internet_address_list_to_string (addresses, true);
  s = utf8ize (pch);
  gtk_entry_set_text (GTK_ENTRY(_to_entry), s.c_str());
  g_free (pch);

  // update 'other headers'
  SetMessageForeachHeaderData data;
  if (message->mime_part && g_mime_header_has_raw (message->mime_part->headers))
    g_mime_header_foreach (message->mime_part->headers, set_message_foreach_header_func, &data);
  g_mime_header_foreach (GMIME_OBJECT(message)->headers, set_message_foreach_header_func, &data);
  s = utf8ize (data.visible_headers);
  gtk_text_buffer_set_text (_headers_buf, s.c_str(), -1);
  _hidden_headers = data.hidden_headers;

  // update body
  int ignored;
  char * tmp = g_mime_message_get_body (message, true, &ignored);
  s = utf8ize (tmp);
  g_free (tmp);
  if (!s.empty()) {
    s = TextMassager().fill (s);
    s += "\n\n";
    gtk_text_buffer_set_text (_body_buf, s.c_str(), s.size());
  }

  // apply the profiles
  update_profile_combobox ();
  apply_profile ();

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

/**
 * We hold off on setting the body textbuffer until after
 * the text view is realized so that GtkTreeView's text wrapping
 * will work properly.
 */
void
PostUI :: body_view_realized_cb (GtkWidget*, gpointer self_gpointer)
{
  PostUI * self = static_cast<PostUI*>(self_gpointer);
  self->set_wrap_mode (self->_prefs.get_flag ("compose-wrap-enabled", true));
  self->set_message (self->_message);
  self->_unchanged_body = self->get_body ();

  if (self->_prefs.get_flag ("always-run-editor", false))
    self->spawn_editor ();

  g_signal_handler_disconnect (self->_body_view, self->body_view_realized_handler);
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
PostUI :: group_entry_changed_cb (GtkEditable*, gpointer ui_gpointer)
{
  PostUI * ui (static_cast<PostUI*>(ui_gpointer));
  unsigned int& tag (ui->_group_entry_changed_idle_tag);
  if (!tag)
    tag = g_timeout_add (2000, group_entry_changed_idle, ui);
}

/***
****
***/

namespace
{
  static void render_from (GtkCellLayout    * ,
                           GtkCellRenderer  * renderer,
                           GtkTreeModel     * model,
                           GtkTreeIter      * iter,
                           gpointer           profiles)
  {
    std::string from;
    std::string name;

    char * key (0);
    gtk_tree_model_get (model, iter, 0, &key, -1);
    if (key) {
      name = key;
      Profile profile;
      if (static_cast<Profiles*>(profiles)->get_profile (key, profile))
        profile.get_from_header (from);
      g_free (key);
    }

    char * name_escaped = g_markup_escape_text (name.c_str(), name.size());
    char * from_escaped = g_markup_escape_text (from.c_str(), from.size());
    char * pch = g_strdup_printf ("%s - <i>%s</i>", from_escaped, name_escaped);
    g_object_set (renderer, "markup", pch, NULL);
    g_free (pch);
    g_free (from_escaped);
    g_free (name_escaped);
  }
}

GtkWidget*
PostUI :: create_main_tab ()
{
  const GtkAttachOptions fill ((GtkAttachOptions)(GTK_FILL|GTK_EXPAND|GTK_SHRINK));
  char buf[512];
  int row = -1;
  GtkWidget *l, *w;
  GtkWidget *t = gtk_table_new (4, 2, false);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);

  // From

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("F_rom"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _from_combo = gtk_combo_box_new_text ();
  gtk_cell_layout_clear (GTK_CELL_LAYOUT(w));
  GtkCellRenderer * r =  gtk_cell_renderer_text_new();
  gtk_cell_layout_pack_start (GTK_CELL_LAYOUT(w), r, true);
  gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT(w), r, render_from, &_profiles, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  g_signal_connect (w, "changed", G_CALLBACK(on_from_combo_changed), this);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Subject

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Subject"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _subject_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Newsgroup

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Newsgroups"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _groups_entry = gtk_entry_new ();
  _group_entry_changed_id = g_signal_connect (w, "changed", G_CALLBACK(group_entry_changed_cb), this);
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Mail To

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("Mail _To"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _to_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fill, fill, 0, 0);

  // Body

  w = create_body_widget (_body_buf, _body_view, _prefs);
  set_spellcheck_enabled (_prefs.get_flag ("spellcheck-enabled", DEFAULT_SPELLCHECK_FLAG));


  GtkWidget * v = gtk_vbox_new (false, PAD);
  gtk_container_set_border_width (GTK_CONTAINER(v), PAD);
  gtk_box_pack_start (GTK_BOX(v), t, false, false, 0);
  pan_box_pack_start_defaults (GTK_BOX(v), w);
  return v;
}

namespace
{
  void message_id_toggled_cb (GtkToggleButton * tb, gpointer prefs_gpointer)
  {
    static_cast<Prefs*>(prefs_gpointer)->set_flag (MESSAGE_ID_PREFS_KEY, gtk_toggle_button_get_active(tb));
  }
  void user_agent_toggled_cb (GtkToggleButton * tb, gpointer prefs_gpointer)
  {
    static_cast<Prefs*>(prefs_gpointer)->set_flag (USER_AGENT_PREFS_KEY, gtk_toggle_button_get_active(tb));
  }
}

GtkWidget*
PostUI :: create_extras_tab ()
{
  const GtkAttachOptions fill ((GtkAttachOptions)(GTK_FILL));
  const GtkAttachOptions fe ((GtkAttachOptions)(GTK_FILL|GTK_EXPAND|GTK_SHRINK));
  char buf[512];
  int row = -1;
  GtkWidget *l, *w;
  GtkWidget *t = gtk_table_new (3, 2, false);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);

  // Followup-To

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("Follo_wup-To"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _followupto_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  /* i18n: "poster" is a key used by many newsreaders.  probably safest to keep this key in english. */
gtk_widget_set_tooltip_text (w, _("The newsgroups where replies to your message should go.  This is only needed if it differs from the \"Newsgroups\" header.\n\nTo direct all replies to your email address, use \"Followup-To: poster\""));
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fe, fill, 0, 0);

  //  Reply-To

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Reply-To"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  w = _replyto_entry = gtk_entry_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
gtk_widget_set_tooltip_text (w, _("The email account where mail replies to your posted message should go.  This is only needed if it differs from the \"From\" header."));
  gtk_table_attach (GTK_TABLE(t), w, 1, 2, row, row+1, fe, fill, 0, 0);

  //  Extra Headers

  ++row;
  g_snprintf (buf, sizeof(buf), "<b>%s:</b>", _("_Custom Headers"));
  l = gtk_label_new_with_mnemonic (buf);
  gtk_label_set_use_markup (GTK_LABEL(l), true);
  gtk_misc_set_alignment (GTK_MISC(l), 0.0f, 0.0f);
  gtk_table_attach (GTK_TABLE(t), l, 0, 1, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  ++row;
  w = gtk_text_view_new ();
  gtk_label_set_mnemonic_widget (GTK_LABEL(l), w);
  _headers_buf = gtk_text_view_get_buffer (GTK_TEXT_VIEW(w));
  gtk_text_view_set_wrap_mode (GTK_TEXT_VIEW(w), GTK_WRAP_NONE);
  gtk_text_view_set_editable (GTK_TEXT_VIEW(w), true);
  GtkWidget * scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER(scroll), w);
  GtkWidget * frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME(frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(frame), scroll);
  gtk_table_attach_defaults (GTK_TABLE(t), frame, 0, 2, row, row+1);


  //  User-Agent

  ++row;
  w = _user_agent_check = gtk_check_button_new_with_mnemonic (_("Add \"_User-Agent\" header"));
  bool b = _prefs.get_flag (USER_AGENT_PREFS_KEY, true);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), b);
  g_signal_connect (w, "toggled", G_CALLBACK(user_agent_toggled_cb), &_prefs);
  gtk_table_attach (GTK_TABLE(t), w, 0, 2, row, row+1, GTK_FILL, GTK_FILL, 0, 0);

  ++row;
  w = _message_id_check = gtk_check_button_new_with_mnemonic (_("Add \"Message-_Id header"));
  b = _prefs.get_flag (MESSAGE_ID_PREFS_KEY, false);
  gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON(w), b);
  g_signal_connect (w, "toggled", G_CALLBACK(message_id_toggled_cb), &_prefs);
  gtk_table_attach (GTK_TABLE(t), w, 0, 2, row, row+1, GTK_FILL, GTK_FILL, 0, 0);


  gtk_container_set_border_width (GTK_CONTAINER(t), PAD);
  gtk_table_set_col_spacings (GTK_TABLE(t), PAD);
  gtk_table_set_row_spacings (GTK_TABLE(t), PAD);
  return t;
}


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
  _followupto_entry (0),
  _replyto_entry (0),
  _body_view (0),
  _body_buf (0),
  _message (0),
  _charset (DEFAULT_CHARSET),
  _group_entry_changed_id (0),
  _group_entry_changed_idle_tag (0)
{
  g_assert (profiles.has_profiles());
  g_return_if_fail (message != 0);

  ua_extra = prefs.get_flag(USER_AGENT_EXTRA_PREFS_KEY, false);

  // create the window
  _root = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  g_signal_connect (_root, "delete-event", G_CALLBACK(delete_event_cb), this);
  gtk_window_set_role (GTK_WINDOW(_root), "pan-post-window");
  gtk_window_set_title (GTK_WINDOW(_root), _("Post Article"));
  gtk_window_set_default_size (GTK_WINDOW(_root), -1, 450);
  g_object_set_data_full (G_OBJECT(_root), "post-ui", this, delete_post_ui);
  if (parent) {
    gtk_window_set_transient_for (GTK_WINDOW(_root), parent);
    gtk_window_set_position (GTK_WINDOW(_root), GTK_WIN_POS_CENTER_ON_PARENT);
  }

  // populate the window
  GtkWidget * vbox = gtk_vbox_new (false, PAD_SMALL);
  GtkWidget * menu_vbox = gtk_vbox_new (false, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX(vbox), menu_vbox, false, false, 0);
  add_actions (menu_vbox);
  gtk_window_add_accel_group (GTK_WINDOW(_root), gtk_ui_manager_get_accel_group (_uim));
  gtk_widget_show_all (vbox);
  gtk_container_add (GTK_CONTAINER(_root), vbox);

  GtkWidget * notebook = gtk_notebook_new ();
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), create_main_tab(), gtk_label_new_with_mnemonic(_("_Message")));
  gtk_notebook_append_page (GTK_NOTEBOOK(notebook), create_extras_tab(), gtk_label_new_with_mnemonic(_("More _Headers")));
  pan_box_pack_start_defaults (GTK_BOX(vbox), notebook);

  // remember this message, but don't put it in the text view yet.
  // we have to wait for it to be realized first so that wrapping
  // will work correctly.
  _message = message;
  g_object_ref (G_OBJECT(_message));
  body_view_realized_handler = g_signal_connect (_body_view, "realize", G_CALLBACK(body_view_realized_cb), this);
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
