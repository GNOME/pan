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

#ifndef _BodyPane_h_
#define _BodyPane_h_

#include "group-prefs.h"
#include "prefs.h"
#include <config.h>
#include <gdk/gdk.h>
#include <gmime/gmime.h>
#include <gtk/gtk.h>
#include <pan/data/article-cache.h>
#include <pan/data/article.h>
#include <pan/data/data.h>
#include <pan/general/quark.h>
#include <pan/gui/header-pane.h>
#include <pan/usenet-utils/gpg.h>
#include <pan/usenet-utils/text-massager.h>

namespace pan {
/**
 * Body Pane in the main window of Pan's GUI.
 * @ingroup GUI
 */
class BodyPane : private Prefs::Listener
{

  private:
    Prefs &_prefs;
    GroupPrefs &_group_prefs;
    Queue &_queue;
    HeaderPane *_header_pane;
    Data &_data;
    ArticleCache &_cache;

    static gboolean on_verbose_tooltip_cb(GtkWidget *widget,
                                          gint x,
                                          gint y,
                                          gboolean keyboard_tip,
                                          GtkTooltip *tooltip,
                                          gpointer data);

  public:
    BodyPane(
      Data &, ArticleCache &, Prefs &, GroupPrefs &, Queue &, HeaderPane *);
    ~BodyPane();

    GtkWidget *root()
    {
      return _root;
    }

    GtkWidget *get_default_focus_widget()
    {
      return _text;
    }

  private:
    void on_prefs_flag_changed(StringView const &key, bool value) override;

    void on_prefs_int_changed(StringView const &key G_GNUC_UNUSED,
                              int value G_GNUC_UNUSED) override
    {
    }

    void on_prefs_string_changed(StringView const &key,
                                 StringView const &value) override;
    void on_prefs_color_changed(StringView const &key,
                                GdkRGBA const &color) override;

  public:
    void set_article(Article const &);
    void clear();

    bool read_more()
    {
      return read_more_or_less(true);
    }

    bool read_less()
    {
      return read_more_or_less(false);
    }

    void rot13_selected_text();
    void select_all();
    GMimeMessage *create_followup_or_reply(bool is_reply);

  public:
    Quark get_message_id() const
    {
      return _article.message_id;
    }

    GMimeMessage *get_message()
    {
      if (_message)
      {
        g_object_ref(_message);
      }
      return _message;
    }

  public:
    enum MenuSelection
    {
      MENU_SAVE_AS,
      MENU_SAVE_ALL
    };

  public:
    void set_character_encoding(char const *character_encoding);

  public:
    void set_text_from_message(GMimeMessage *);

  private:
    void refresh();
    void refresh_fonts();
    void refresh_colors();
    bool read_more_or_less(bool more);
    char *body_to_utf8(GMimePart *);
    void append_part(GMimeObject *, GMimeObject *, GtkAllocation *);
    static gboolean expander_activated_idle(gpointer self);
    static void expander_activated_cb(GtkExpander *, gpointer self);
    static void verbose_clicked_cb(GtkWidget *,
                                   GdkEvent *event,
                                   gpointer self_gpointer);
    static void foreach_part_cb(GMimeObject *, GMimeObject *, gpointer self);
    static void text_size_allocated(GtkWidget *, GtkAllocation *, gpointer);
    static gboolean text_size_allocated_idle_cb(gpointer p);
    void text_size_allocated_idle();
    void refresh_scroll_visible_state();
    static gboolean show_idle_cb(gpointer p);
    static void show_cb(GtkWidget *, gpointer);
    static void populate_popup_cb(GtkTextView *, GtkMenu *, gpointer);
    void populate_popup(GtkTextView *, GtkMenu *);
    static void copy_url_cb(GtkMenuItem *, gpointer);
    void copy_url();

    GtkWidget *create_attachments_toolbar(GtkWidget *);

  private:
#ifdef HAVE_WEBKIT
    void set_html_text(const char *text);
    GtkWidget *_web_view;
#endif
  private:
    void add_attachment_to_toolbar(char const *fn);
    void clear_attachments();
    GtkWidget *new_attachment(char const *filename);

    static gboolean mouse_button_pressed_cb(GtkWidget *,
                                            GdkEventButton *,
                                            gpointer);
    gboolean mouse_button_pressed(GtkWidget *, GdkEventButton *);
    void menu_clicked(MenuSelection const &ms);
    static void menu_clicked_as_cb(GtkWidget *w, gpointer p);
    static void menu_clicked_all_cb(GtkWidget *w, gpointer p);

  private:
    std::string _hover_url;
    GtkWidget *_expander;
    GtkWidget *_terse;
    GtkWidget *_verbose;
    GtkWidget *_headers;
    GtkWidget *_xface;
    GtkWidget *_face;
    GtkTextBuffer *_buffer;
    GtkWidget *_root;
    GtkWidget *_text;
    GtkWidget *_scroll;
    GtkWidget *_att_toolbar;
    GtkWidget *_att_box;
    bool _hscroll_visible;
    bool _vscroll_visible;
    Article _article;
    GMimeMessage *_message;
    TextMassager _tm;
    std::string _charset;
#ifdef HAVE_GMIME_CRYPTO
    GPGDecErr _gpgerr;
#endif
    int _attachments;
    int _cur_col, _cur_row;
    std::set<char *> _attach_names;
    MenuSelection _selection;

    bool _cleared;

  public:
    void set_cleared(bool val)
    {
      _cleared = val;
    }

    bool get_cleared()
    {
      return _cleared;
    }

  public:
    char const *_current_attachment;

  public:
    GtkWidget *_menu;
};
} // namespace pan

#endif
