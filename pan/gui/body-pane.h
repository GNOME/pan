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

#ifndef _BodyPane_h_
#define _BodyPane_h_

#include <gtk/gtk.h>
#include <gmime/gmime.h>
#include <pan/general/quark.h>
#include <pan/usenet-utils/text-massager.h>
#include <pan/data/article.h>
#include <pan/data/article-cache.h>
#include <pan/data/data.h>
#include "prefs.h"

namespace pan
{
  /**
   * Body Pane in the main window of Pan's GUI.
   * @ingroup GUI
   */
  class BodyPane: private Prefs::Listener
  {
    private:
      Prefs& _prefs;
      Data& _data;
      ArticleCache& _cache;

    public:
      BodyPane (Data&, ArticleCache&, Prefs&);
      ~BodyPane ();
      GtkWidget* root () { return _root; }

    private:
      virtual void on_prefs_flag_changed (const StringView& key, bool value);
      virtual void on_prefs_int_changed (const StringView& key, int value) { }
      virtual void on_prefs_string_changed (const StringView& key, const StringView& value);
      virtual void on_prefs_color_changed (const StringView& key, const GdkColor& color);

    public:
      void set_article (const Article&);
      void clear ();
      bool read_more () { return read_more_or_less(true); }
      bool read_less () { return read_more_or_less(false); }
      void rot13_selected_text ();
      void select_all ();
      GMimeMessage* create_followup_or_reply (bool is_reply);

    public:
      Quark get_message_id () const {
        return _article.message_id;
      }
      GMimeMessage* get_message () {
        if (_message)
          g_object_ref (_message);
        return _message;
      }

    public:
      void set_character_encoding (const char * character_encoding);

    private:
      void refresh ();
      void refresh_fonts ();
      void refresh_colors ();
      bool read_more_or_less (bool more);
      char* body_to_utf8 (GMimePart*);
      void set_text_from_message (GMimeMessage*);
      void append_part (GMimeObject*, GtkAllocation*);
      static gboolean expander_activated_idle (gpointer self);
      static void expander_activated_cb (GtkExpander*, gpointer self);
      static void foreach_part_cb (GMimeObject*, gpointer self);
      static void text_size_allocated (GtkWidget*, GtkAllocation*, gpointer);
      static gboolean text_size_allocated_idle_cb (gpointer p);
      void text_size_allocated_idle ();
      void refresh_scroll_visible_state ();
      static gboolean show_idle_cb (gpointer p);
      static void show_cb (GtkWidget*, gpointer);

    private:
      GtkWidget * _expander;
      GtkWidget * _terse;
      GtkWidget * _verbose;
      GtkWidget * _headers;
      GtkWidget * _xface;
      GtkTextBuffer * _buffer;
      GtkWidget * _root;
      GtkWidget * _text;
      GtkWidget * _scroll;
      bool _hscroll_visible;
      bool _vscroll_visible;
      Article _article;
      GMimeMessage * _message;
      TextMassager _tm;
      std::string _charset;
  };
}

#endif
