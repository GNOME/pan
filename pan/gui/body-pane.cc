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
#include <cctype>
#include <cmath>
#include <iostream>
#include <sstream>
extern "C" {
  #include <glib/gi18n.h>
  #include <gtk/gtk.h>
  #include <gdk/gdk.h>
  #include <gdk/gdkkeysyms.h>
  #include <gmime/gmime.h>
}
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <pan/general/debug.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include <pan/general/e-util.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/url-find.h>
#include <pan/icons/pan-pixbufs.h>
#include "body-pane.h"
#include "pad.h"
#include "tango-colors.h"
#include "xface.h"
#include "url.h"
#include "gtk_compat.h"

#define FIRST_PICTURE "first-picture"

using namespace pan;

/***
****
***/

namespace
{

  enum Icons
  {
    ICON_SIG_OK,
    ICON_SIG_FAIL,
    NUM_ICONS
  };

  struct Icon {
  const guint8 * pixbuf_txt;
  GdkPixbuf * pixbuf;
  } icons[NUM_ICONS] = {
    { icon_sig_ok,          0 },
    { icon_sig_fail,        0 }
  };
}

/***
****
***/

/**
***  Pixbuf Cache
**/
namespace
{
  class PixbufCache
  {
    private:
      typedef std::pair<GdkPixbuf*,GdkPixbuf*> pixbuf_pair_t; // original, scaled
      typedef std::map<int,pixbuf_pair_t> pixbufs_t;
      pixbufs_t _pixbufs;

    public:
      PixbufCache() {}
      ~PixbufCache() { clear(); }

    public:
      void clear () {
        foreach (pixbufs_t, _pixbufs, it) {
          g_object_unref (it->second.first);
          g_object_unref (it->second.second);
        }
        _pixbufs.clear ();
      }

    public:
      bool get_pixbuf_at_offset (int offset, GdkPixbuf*& original, GdkPixbuf*& scaled) {
        if (!_pixbufs.count(offset))
          return false;
        pixbuf_pair_t &p(_pixbufs[offset]);
        original = p.first;
        scaled = p.second;
        return true;
      }
      void set_pixbuf_at_offset (int offset, GdkPixbuf *original, GdkPixbuf *scaled) {
        const bool had_old (_pixbufs.count(offset));
        pixbuf_pair_t &p(_pixbufs[offset]), old(p);
        g_object_ref (p.first = original);
        g_object_ref (p.second = scaled);
        if (had_old) {
          g_object_unref (old.first);
          g_object_unref (old.second);
        }
      }
  };

  void delete_pixbuf_cache (gpointer pc) {
    delete static_cast<PixbufCache*>(pc);
  }

  PixbufCache& get_pixbuf_cache (gpointer gp) {
    static const char * PIXBUF_CACHE ("pixbuf-cache");
    GObject * o (G_OBJECT (gp));
    PixbufCache *pc ((PixbufCache*) g_object_get_data(o, PIXBUF_CACHE));
    if (!pc) {
      pc = new PixbufCache ();
      g_object_set_data_full (o, PIXBUF_CACHE, pc, delete_pixbuf_cache);
    }
    return *pc;
  }

  bool get_pixbuf_at_offset (gpointer o, int offset, GdkPixbuf*& original, GdkPixbuf*& scaled) {
    return get_pixbuf_cache(o).get_pixbuf_at_offset (offset, original, scaled);
  }

  void set_pixbuf_at_offset (gpointer o, int offset, GdkPixbuf *original, GdkPixbuf *scaled) {
    get_pixbuf_cache(o).set_pixbuf_at_offset (offset, original, scaled);
  }

  void clear_pixbuf_cache (gpointer o) {
    get_pixbuf_cache(o).clear ();
  }
}

/**
***  "fullsize" flag
**/
namespace
{
  #define FULLSIZE "fullsize"

  void set_fullsize_flag (gpointer o, bool b) {
    g_object_set_data (G_OBJECT(o), FULLSIZE, GINT_TO_POINTER(b));
  }
  bool get_fullsize_flag (gpointer o) {
    return g_object_get_data (G_OBJECT(o), FULLSIZE) != 0;
  }
  bool toggle_fullsize_flag (gpointer o) {
    const bool b (!get_fullsize_flag (o));
    set_fullsize_flag (o, b);
    return b;
  }
}

/**
***  Cursors
**/
namespace
{
  enum {
    CURSOR_IBEAM,
    CURSOR_HREF,
    CURSOR_ZOOM_IN,
    CURSOR_ZOOM_OUT,
    CURSOR_QTY
  };

  GdkCursor * cursors[CURSOR_QTY];
  GdkCursor * cursor_current (0);

  void free_cursors (void)
  {
    for (int i=0; i<CURSOR_QTY; ++i)
      gdk_cursor_unref (cursors[i]);
  }

  void ensure_cursors_created (GtkWidget * w)
  {
    static bool created (false);
    if (!created)
    {
      created = true;
      GdkDisplay * display (gtk_widget_get_display (w));

      int width, height;
      const GtkIconSize size (GTK_ICON_SIZE_LARGE_TOOLBAR);
#if !GTK_CHECK_VERSION(3,0,0)
      GtkStyle * style (gtk_widget_get_style (w));
      const GtkTextDirection dir (GTK_TEXT_DIR_NONE);
      const GtkStateType state (GTK_STATE_PRELIGHT);

      GtkIconSet * icon_set = gtk_style_lookup_icon_set (style, GTK_STOCK_ZOOM_IN);
      GdkPixbuf * pixbuf = gtk_icon_set_render_icon (icon_set, style, dir, state, size, w, NULL);
      g_object_get (G_OBJECT(pixbuf), "width", &width, "height", &height, NULL);
      cursors[CURSOR_ZOOM_IN] = gdk_cursor_new_from_pixbuf (display, pixbuf, width/2, height/2);
      g_object_unref (G_OBJECT(pixbuf));

      icon_set = gtk_style_lookup_icon_set (style, GTK_STOCK_ZOOM_OUT);
      pixbuf = gtk_icon_set_render_icon (icon_set, style, dir, state, size, w, NULL);
      g_object_get (G_OBJECT(pixbuf), "width", &width, "height", &height, NULL);
      cursors[CURSOR_ZOOM_OUT] = gdk_cursor_new_from_pixbuf (display, pixbuf, width/2, height/2);
      g_object_unref (G_OBJECT(pixbuf));
#else
      GdkPixbuf * pixbuf = gtk_widget_render_icon_pixbuf (w, GTK_STOCK_ZOOM_IN, size);
      g_object_get (G_OBJECT(pixbuf), "width", &width, "height", &height, NULL);
      cursors[CURSOR_ZOOM_IN] = gdk_cursor_new_from_pixbuf (display, pixbuf, width/2, height/2);
      g_object_unref (G_OBJECT(pixbuf));

      pixbuf = gtk_widget_render_icon_pixbuf (w, GTK_STOCK_ZOOM_OUT, size);
      g_object_get (G_OBJECT(pixbuf), "width", &width, "height", &height, NULL);
      cursors[CURSOR_ZOOM_OUT] = gdk_cursor_new_from_pixbuf (display, pixbuf, width/2, height/2);
      g_object_unref (G_OBJECT(pixbuf));
#endif

      cursors[CURSOR_IBEAM] = gdk_cursor_new (GDK_XTERM);
      cursors[CURSOR_HREF] = gdk_cursor_new (GDK_HAND2);

      g_atexit (free_cursors);
    }
  }

  void set_cursor (GdkWindow *window, GtkWidget *w, int mode)
  {
    ensure_cursors_created (w);
    GdkCursor * cursor_new = cursors[mode];
    if (cursor_new != cursor_current)
      gdk_window_set_cursor (window, cursor_current=cursor_new);
  }

  void set_cursor_from_iter (GdkWindow *window, GtkWidget *w, GtkTextIter *it)
  {
    GtkTextView * text_view (GTK_TEXT_VIEW (w));
    GtkTextBuffer * buf (gtk_text_view_get_buffer (text_view));
    GtkTextTagTable * tags (gtk_text_buffer_get_tag_table (buf));
    GtkTextTag * pix_tag (gtk_text_tag_table_lookup (tags, "pixbuf"));
    GtkTextTag * url_tag (gtk_text_tag_table_lookup (tags, "url"));
    const bool in_url (gtk_text_iter_has_tag (it, url_tag));
    const bool in_pix (gtk_text_iter_has_tag (it, pix_tag));
    const bool fullsize (get_fullsize_flag (buf));

    int mode;
    if (in_pix && fullsize) mode = CURSOR_ZOOM_OUT;
    else if (in_pix)        mode = CURSOR_ZOOM_IN;
    else if (in_url)        mode = CURSOR_HREF;
    else                    mode = CURSOR_IBEAM;
    set_cursor (window, w, mode);
  }
}


namespace
{
  GtkTextTag* get_named_tag_from_view (GtkWidget * w, const char * key)
  {
    GtkTextView * text_view (GTK_TEXT_VIEW(w));
    GtkTextBuffer * buf = gtk_text_view_get_buffer (text_view);
    GtkTextTagTable * tags = gtk_text_buffer_get_tag_table (buf);
    return gtk_text_tag_table_lookup (tags, key);
  }

  void get_iter_from_event_coords (GtkWidget * w,
                                   int x, int y,
                                   GtkTextIter * setme)
  {
    GtkTextView * text_view (GTK_TEXT_VIEW (w));
    gtk_text_view_window_to_buffer_coords (text_view,
                                           GTK_TEXT_WINDOW_WIDGET,
                                           x, y, &x, &y);
    gtk_text_view_get_iter_at_location (text_view, setme, x, y);
  }

  std::string get_url_from_iter (GtkWidget * w, GtkTextIter * iter)
  {
    std::string ret;
    GtkTextTag * url_tag (get_named_tag_from_view (w, "url"));
    if (gtk_text_iter_has_tag (iter, url_tag))
    {
      GtkTextIter begin(*iter), end(*iter);
      if (!gtk_text_iter_begins_tag (&begin, url_tag))
        gtk_text_iter_backward_to_tag_toggle (&begin, NULL);
      gtk_text_iter_forward_to_tag_toggle (&end, NULL);
      char * pch = gtk_text_iter_get_text (&begin, &end);
      if (pch) {
        ret = pch;
        g_free (pch);
      }
    }
    return ret;
  }

  gboolean motion_notify_event (GtkWidget       * w,
                                GdkEventMotion  * event,
                                gpointer          hover_url)
  {
    if (event->window != NULL)
    {
      int x, y;
      if (event->is_hint)
        gdk_window_get_pointer (event->window, &x, &y, NULL);
      else {
        x = (int) event->x;
        y = (int) event->y;
      }
      GtkTextIter iter;
      get_iter_from_event_coords (w, x, y, &iter);
      set_cursor_from_iter (event->window, w, &iter);
      *static_cast<std::string*>(hover_url) = get_url_from_iter (w, &iter);
    }

    return false;
  }

  /* returns a GdkPixbuf of the scaled image.
     unref it when no longer needed. */
  GdkPixbuf* size_to_fit (GdkPixbuf           * pixbuf,
                          const GtkAllocation * size)
  {
    const int nw (size ? size->width : 0);
    const int nh (size ? size->height : 0);

    GdkPixbuf * out (0);
    if (nw>=100 && nh>=100)
    {
      const int ow (gdk_pixbuf_get_width (pixbuf));
      const int oh (gdk_pixbuf_get_height (pixbuf));
      double scale_factor (std::min (nw/(double)ow, nh/(double)oh));
      scale_factor = std::min (scale_factor, 1.0);
      const int scaled_width ((int) std::floor (ow * scale_factor + 0.5));
      const int scaled_height ((int) std::floor (oh * scale_factor + 0.5));
      out = gdk_pixbuf_scale_simple (pixbuf,
                                     scaled_width, scaled_height,
                                     GDK_INTERP_BILINEAR);
    }

    if (!out)
    {
      g_object_ref (pixbuf);
      out = pixbuf;
    }

    return out;
  }

  void resize_picture_at_iter (GtkTextBuffer        * buf,
                               GtkTextIter          * iter,
                               bool                   fullsize,
                               const GtkAllocation  * size,
                               GtkTextTag           * apply_tag)
  {
    const int begin_offset (gtk_text_iter_get_offset (iter));

    GdkPixbuf * original (0);
    GdkPixbuf * old_scaled (0);
    if (!get_pixbuf_at_offset (buf, begin_offset, original, old_scaled))
      return;

    GdkPixbuf * new_scaled (size_to_fit (original, (fullsize ? 0 : size)));
    const int old_w (gdk_pixbuf_get_width (old_scaled));
    const int new_w (gdk_pixbuf_get_width (new_scaled));
    const int old_h (gdk_pixbuf_get_height (old_scaled));
    const int new_h (gdk_pixbuf_get_height (new_scaled));
    if (old_w!=new_w || old_h!=new_h)
    {
      // remove the old..
      GtkTextIter old_end (*iter);
      gtk_text_iter_forward_to_tag_toggle (&old_end, apply_tag);
      gtk_text_buffer_delete (buf, iter, &old_end);

      // insert the new..
      gtk_text_buffer_insert_pixbuf (buf, iter, new_scaled);
      gtk_text_buffer_insert (buf, iter, "\n", -1);
      set_pixbuf_at_offset (buf, begin_offset, original, new_scaled);

      // and apply the tag.
      GtkTextIter begin (*iter);
      gtk_text_iter_set_offset (&begin, begin_offset);
      gtk_text_buffer_apply_tag (buf, apply_tag, &begin, iter);
    }

    g_object_unref (new_scaled);
  }
}

gboolean
BodyPane :: mouse_button_pressed_cb (GtkWidget *w, GdkEventButton *e, gpointer p)
{
  return static_cast<BodyPane*>(p)->mouse_button_pressed (w, e);
}

gboolean
BodyPane :: mouse_button_pressed (GtkWidget *w, GdkEventButton *event)
{
  g_return_val_if_fail (GTK_IS_TEXT_VIEW(w), false);

  if (event->button==1 || event->button==2) {
    const std::string& url (_hover_url);
    if (!url.empty()) {
      /* this is a crude way of making sure that double-click
       * doesn't open two or three browser windows. */
      static time_t last_url_time (0);
      const time_t this_url_time (time (0));
      if (this_url_time != last_url_time) {
        last_url_time = this_url_time;
        URL :: open (_prefs, url.c_str());
      }
    } else { // maybe we're zooming in/out on a pic...
      GtkTextIter iter;
      GtkTextTag * pix_tag (get_named_tag_from_view (w, "pixbuf"));
      get_iter_from_event_coords (w, (int)event->x, (int)event->y, &iter);
      if (gtk_text_iter_has_tag (&iter, pix_tag))
      {
        if (!gtk_text_iter_begins_tag (&iter, pix_tag))
          gtk_text_iter_backward_to_tag_toggle (&iter, pix_tag);
        g_assert (gtk_text_iter_begins_tag (&iter, pix_tag));

        // percent_x,percent_y reflect where the user clicked in the picture
        GdkRectangle rec;
        gtk_text_view_get_iter_location (GTK_TEXT_VIEW(w), &iter, &rec);
        int buf_x, buf_y;
        gtk_text_view_window_to_buffer_coords (GTK_TEXT_VIEW(w), GTK_TEXT_WINDOW_WIDGET,
                                               (gint)event->x, (gint)event->y, &buf_x, &buf_y);
        const double percent_x = (buf_x - rec.x) / (double)rec.width;
        const double percent_y = (buf_y - rec.y) / (double)rec.height;

         // resize the picture and refresh `iter'
        const int offset (gtk_text_iter_get_offset (&iter));
        GtkTextBuffer * buf (gtk_text_view_get_buffer (GTK_TEXT_VIEW(w)));
        const bool fullsize (toggle_fullsize_flag (buf));
        GtkAllocation aloc;
        gtk_widget_get_allocation(w, &aloc);
        resize_picture_at_iter (buf, &iter, fullsize, &aloc, pix_tag);
        gtk_text_iter_set_offset (&iter, offset);
        set_cursor_from_iter (event->window, w, &iter);

        // x2,y2 are to position percent_x,percent_y in the middle of the window.
        GtkTextMark * mark = gtk_text_buffer_create_mark (buf, NULL, &iter, true);
        const double x2 = CLAMP ((percent_x + (percent_x - 0.5)), 0.0, 1.0);
        const double y2 = CLAMP ((percent_y + (percent_y - 0.5)), 0.0, 1.0);
        gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW(w), mark, 0.0, true, x2, y2);
        gtk_text_buffer_delete_mark (buf, mark);
      }
    }
  }
  return false;
}

/***
****  INIT
***/
namespace
{
  GtkTextTag* get_or_create_tag (GtkTextTagTable * table, const char * key)
  {
    g_assert (table);
    g_assert (key && *key);

    GtkTextTag * tag (gtk_text_tag_table_lookup (table, key));
    if (!tag) {
      tag = gtk_text_tag_new (key);
      gtk_text_tag_table_add (table, tag);
      g_object_unref (tag); // table refs it
    }
    return tag;
  }

  void
  set_text_buffer_tags (GtkTextBuffer * buffer, const Prefs& p)
  {
    GtkTextTagTable * table = gtk_text_buffer_get_tag_table (buffer);
    get_or_create_tag (table, "pixbuf");
    get_or_create_tag (table, "quote_0");
    g_object_set (get_or_create_tag(table,"bold"),
      "weight", PANGO_WEIGHT_BOLD,
      NULL);
    g_object_set (get_or_create_tag (table, "italic"),
      "style", PANGO_STYLE_ITALIC,
      NULL);
    g_object_set (get_or_create_tag (table, "underline"),
      "underline", PANGO_UNDERLINE_SINGLE,
      NULL);
    g_object_set (get_or_create_tag (table, "url"),
      "underline", PANGO_UNDERLINE_SINGLE,
      "foreground", p.get_color_str ("body-pane-color-url", TANGO_SKY_BLUE_DARK).c_str(),
      NULL);
    g_object_set (get_or_create_tag (table, "quote_1"),
      "foreground", p.get_color_str ("body-pane-color-quote-1", TANGO_CHAMELEON_DARK).c_str(),
      NULL);
    g_object_set (get_or_create_tag (table, "quote_2"),
      "foreground", p.get_color_str ("body-pane-color-quote-2", TANGO_ORANGE_DARK).c_str(),
      NULL);
    g_object_set (get_or_create_tag (table, "quote_3"),
      "foreground", p.get_color_str ("body-pane-color-quote-3", TANGO_PLUM_DARK).c_str(),
      NULL);
    g_object_set (get_or_create_tag (table, "signature"),
      "foreground", p.get_color_str ("body-pane-color-signature", TANGO_SKY_BLUE_LIGHT).c_str(),
      NULL);
  }
}

/***
****
***/
namespace
{
  // handle up, down, pgup, pgdown to scroll
  gboolean text_key_pressed (GtkWidget * w, GdkEventKey * event, gpointer scroll)
  {
    gboolean handled (false);

    g_return_val_if_fail (GTK_IS_TEXT_VIEW(w), false);

//    if (event->keyval==GDK_KEY_Enter) return false;

    const bool up = event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_KP_Up;
    const bool down = event->keyval==GDK_KEY_Down || event->keyval==GDK_KEY_KP_Down;

    if (up || down)
    {
      handled = true;
      gtk_text_view_place_cursor_onscreen (GTK_TEXT_VIEW(w));
      GtkAdjustment * adj = gtk_scrolled_window_get_vadjustment (
                                                      GTK_SCROLLED_WINDOW(scroll));
      gdouble val = gtk_adjustment_get_value(adj);
      if (up)
        val -= gtk_adjustment_get_step_increment(adj);
      else
        val += gtk_adjustment_get_step_increment(adj);
      val = MAX(val, gtk_adjustment_get_lower(adj) );
      val = MIN(val, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj) );
      gtk_adjustment_set_value (adj, val);
    }

    return handled;
  }
}

/****
*****
*****   SETTING THE TEXT FROM AN ARTICLE
*****
****/
namespace
{
  bool text_get_show_all_headers () { return false; }

  /**
   * Returns the quote tag ("quote_0", "quote_1", etc) appropriate for the line.
   * The right tag is calculated by adding up the number of quote characters
   * at the beginning of the line.
   *
   * @param utf8_line the line whose quote status we're checking
   * @param utf8_byte_len the byte length of utf8_line
   * @return a const string for the line's quote tag.  Never NULL.
   */
  const char * get_quote_tag (const TextMassager     * text_massager,
                              const char             * utf8_line,
                              int                      utf8_byte_len)
  {
    const char * str = utf8_line;
    const char * line_end = utf8_line + utf8_byte_len;
    const char * retval = "quote_0";

    if (0<utf8_byte_len && str && *str)
    {
      int depth = 0;

      // walk past leading spaces
      while (str!=line_end && g_unichar_isspace(g_utf8_get_char(str)))
        str = g_utf8_next_char (str);

      // count the number of spaces or quote characters
      for (;;) {
        if (str == line_end)
          break;
        else if (text_massager->is_quote_character (g_utf8_get_char (str)))
          ++depth;
        else if (!g_unichar_isspace(g_utf8_get_char(str)))
          break;
        str = g_utf8_next_char (str);
      }

      if (!depth)
        retval = "quote_0";
      else switch (depth % 3) {
        case 1: retval = "quote_1"; break;
        case 2: retval = "quote_2"; break;
        case 0: retval = "quote_3"; break;
      }
    }

    return retval;
  }

  typedef std::map<std::string,GdkPixbuf*> pixbufs_t;

  // don't use this directly -- use get_emoticons()
  pixbufs_t emoticon_pixbufs;

  void clear_emoticon_pixbufs ()
  {
    foreach_const (pixbufs_t, emoticon_pixbufs, it)
      g_object_unref (it->second);
  }

  pixbufs_t& get_emoticons ()
  {
    static bool inited (false);
    if (!inited) {
      inited = true;
      emoticon_pixbufs[":)"] = gdk_pixbuf_new_from_inline (-1, icon_mozilla_smile, false, 0);
      emoticon_pixbufs[":-)"] = gdk_pixbuf_new_from_inline (-1, icon_mozilla_smile, false, 0);
      emoticon_pixbufs[";)"] = gdk_pixbuf_new_from_inline (-1, icon_mozilla_wink, false, 0);
      emoticon_pixbufs[":("] = gdk_pixbuf_new_from_inline (-1, icon_mozilla_frown, false, 0);
      emoticon_pixbufs[":P"] = gdk_pixbuf_new_from_inline (-1, icon_mozilla_tongueout, false, 0);
      emoticon_pixbufs[":O"] = gdk_pixbuf_new_from_inline (-1, icon_mozilla_surprised, false, 0);
      g_atexit (clear_emoticon_pixbufs);
    }
    return emoticon_pixbufs;
  }

  enum TagMode { ADD, REPLACE };

  void
  set_section_tag (GtkTextBuffer     * buffer,
                   GtkTextIter       * start,
                   const StringView  & body,
                   const StringView  & area,
                   const char        * tag,
                   TagMode             mode)
  {
    // if no alnums, chances are area is a false positive
    int alnums (0);
    for (const char *pch(area.begin()), *end(area.end()); pch!=end; ++pch)
      if (::isalnum(*pch))
        ++alnums;
    if (!alnums)
      return;

    GtkTextIter mark_start = *start;
    gtk_text_iter_forward_chars (&mark_start,
                                 g_utf8_strlen(body.str, area.str-body.str));
    GtkTextIter mark_end = mark_start;
    gtk_text_iter_forward_chars (&mark_end, g_utf8_strlen(area.str,area.len));
    if (mode == REPLACE)
      gtk_text_buffer_remove_all_tags (buffer, &mark_start, &mark_end);
    gtk_text_buffer_apply_tag_by_name (buffer, tag, &mark_start, &mark_end);
  }

  void
  replace_emoticon_text_with_pixbuf (GtkTextBuffer      * buffer,
                                     GtkTextMark        * mark,
                                     std::string        & body,
                                     const std::string  & text,
                                     GdkPixbuf          * pixbuf)
  {
    g_assert (!text.empty());
    g_assert (pixbuf != 0);

    GtkTextTagTable * tags (gtk_text_buffer_get_tag_table (buffer));
    GtkTextTag * url_tag (gtk_text_tag_table_lookup (tags, "url"));

    const size_t n (text.size());
    std::string::size_type pos (0);
    while (((pos=body.find(text,pos))) != body.npos)
    {
      GtkTextIter begin;
      gtk_text_buffer_get_iter_at_mark (buffer, &begin, mark);
      gtk_text_iter_forward_chars (&begin, g_utf8_strlen(&body[0],pos));

      if (gtk_text_iter_has_tag (&begin, url_tag))
        pos += n;
      else {
        GtkTextIter end (begin);
        gtk_text_iter_forward_chars (&end, n);
        gtk_text_buffer_delete (buffer, &begin, &end);
        body.erase (pos, text.size());
        gtk_text_buffer_insert_pixbuf (buffer, &end, pixbuf);
        body.insert (pos, 1, '?'); // make body.size() match the textbuf's size
      }
    }
  }

  /**
   * Appends the specified body into the text buffer.
   * This function takes care of muting quotes and marking
   * quoted and URL areas in the GtkTextBuffer.
   */
  void
  append_text_buffer_nolock (const TextMassager  * text_massager,
                             GtkTextBuffer       * buffer,
                             const StringView    & body_in,
                             bool                  mute_quotes,
                             bool                  show_smilies,
                             bool                  do_markup,
                             bool                  do_urls)
  {
    g_return_if_fail (buffer!=0);
    g_return_if_fail (GTK_IS_TEXT_BUFFER(buffer));

    // mute the quoted text, if desired
    std::string body;
    if (mute_quotes)
      body = text_massager->mute_quotes (body_in);
    else
      body.assign (body_in.str, body_in.len);

    // insert the text
    GtkTextIter end;
    gtk_text_buffer_get_end_iter (buffer, &end);
    GtkTextMark * mark = gtk_text_buffer_create_mark (buffer, "blah", &end, true);
    gtk_text_buffer_insert (buffer, &end, body.c_str(), -1);
    GtkTextIter start;
    gtk_text_buffer_get_iter_at_mark (buffer, &start, mark);

    StringView v(body), line;
    GtkTextIter mark_start (start);

    // find where the signature begins...
    const char * sig_point (0);
    int offset (0);
    if (GNKSA::find_signature_delimiter (v, offset) != GNKSA::SIG_NONE)
      sig_point = v.str + offset;

    // colorize the quoted text
    GtkTextIter mark_end;
    std::string last_quote_tag;
    bool is_sig (false);
    const char * last_quote_begin (v.str);
    while (v.pop_token (line, '\n'))
    {
      if (line.empty())
        continue;

      if (line.str == sig_point)
        is_sig = true;

      const std::string quote_tag = is_sig
        ?  "signature"
        : get_quote_tag (text_massager, line.str, line.len);

      // if we've changed tags, colorize the previous block
      if (!last_quote_tag.empty() && quote_tag!=last_quote_tag) {
        mark_end = mark_start;
        gtk_text_iter_forward_chars (&mark_end, g_utf8_strlen(last_quote_begin,line.str-1-last_quote_begin));
        gtk_text_buffer_apply_tag_by_name (buffer, last_quote_tag.c_str(), &mark_start, &mark_end);
        mark_start = mark_end;
        gtk_text_iter_forward_chars (&mark_start, 1);
        last_quote_begin = line.str;
      }

      last_quote_tag = quote_tag;
    }

    // apply the final tag, if there is one */
    if (!last_quote_tag.empty()) {
      gtk_text_buffer_get_end_iter (buffer, &mark_end);
      gtk_text_buffer_apply_tag_by_name (buffer, last_quote_tag.c_str(), &mark_start, &mark_end);
    }

    // apply markup
    const StringView v_all (body);
    if (do_markup)
    {
      bool is_patch = false;
      std::set<const char*> already_processed;
      StringView v(body), line;
      while (v.pop_token (line, '\n'))
      {
        // if we detect that this message contains a patch,
        // turn off markup for the rest of the article.
        if (!is_patch)
          is_patch = (line.strstr("--- ")==line.str) || (line.strstr("@@ ")==line.str);
        if (is_patch)
          continue;

        for (;;)
        {
          // find the first markup character.
          // if we've already used it,
          // or if it was preceeded by something not a space or punctuation,
          // then keep looking.
          const char * b (line.strpbrk ("_*/"));
          if (!b)
            break;
          if (already_processed.count(b) ||
              (b!=&line.front() && !isspace(b[-1]) && !ispunct(b[-1]))) {
            line.eat_chars (b+1-line.str);
            continue;
          }

          // find the ending corresponding markup character.
          // if it was followed by something not a space or punctuation, keep looking.
          const char * e = b;
          while ((e = line.strchr (*b, 1+(e-line.str)))) {
            if (e==&line.back() || isspace(e[1]) || ispunct(e[1]) || strchr("_*/", e[1]))
              break;
          }

          if (e) {
            already_processed.insert (e);
            const char * type (0);
            switch (*b) {
              case '*': type = "bold"; break;
              case '_': type = "underline"; break;
              case '/': type = "italic"; break;
            }
            set_section_tag (buffer, &start, v_all, StringView(b,e+1), type, ADD);
          }

          line.eat_chars (b+1-line.str);
        }
      }
    }

    // colorize urls
    if (do_urls) {
      StringView area;
      StringView march (v_all);
      while ((url_find (march, area))) {
        set_section_tag (buffer, &start, v_all, area, "url", REPLACE);
        march = march.substr (area.str + area.len, 0);
      }
    }

    // do this last, since it alters the text instead of just marking it up
    if (show_smilies) {
      pixbufs_t& emoticons (get_emoticons());
      foreach_const (pixbufs_t, emoticons, it)
        replace_emoticon_text_with_pixbuf (buffer, mark, body, it->first, it->second);
    }

    gtk_text_buffer_delete_mark (buffer, mark);
  }

  /**
   * Generates a GtkPixmap object from a given GMimePart that contains an image.
   * Used for displaying attached pictures inline.
   */
  GdkPixbuf* get_pixbuf_from_gmime_part (GMimePart * part)
  {
    GdkPixbufLoader * l (gdk_pixbuf_loader_new ());
    GError * err (0);

    // populate the loader
    GMimeDataWrapper * wrapper (g_mime_part_get_content_object (part));
    if (wrapper)
    {
      GMimeStream * mem_stream (g_mime_stream_mem_new ());
      g_mime_data_wrapper_write_to_stream (wrapper, mem_stream);
      GByteArray * buffer (GMIME_STREAM_MEM(mem_stream)->buffer);
      gsize bytesLeft = buffer->len;
      guchar * data = buffer->data;

      // ticket #467446 - workaround gdkpixbuf <= 2.12.x's
      // jpg loader bug (#494667) by feeding the loader in
      // smaller chunks
      while( bytesLeft > 0 )
      {
          const gsize n = MIN( 4096, bytesLeft );

          gdk_pixbuf_loader_write (l, data, n, &err);
          if (err) {
            Log::add_err (err->message);
            g_clear_error (&err);
            break;
          }

          bytesLeft -= n;
          data += n;
      }

      g_object_unref (mem_stream);
    }

    // flush the loader
    gdk_pixbuf_loader_close (l, &err);
    if (err) {
      Log::add_err (err->message);
      g_clear_error (&err);
    }

    // create the pixbuf
    GdkPixbuf * pixbuf (0);
    if (!err)
      pixbuf = gdk_pixbuf_loader_get_pixbuf (l);
    else {
      Log::add_err (err->message);
      g_clear_error (&err);
    }

    // cleanup
    if (pixbuf)
      g_object_ref (G_OBJECT(pixbuf));
    g_object_unref (G_OBJECT(l));
    return pixbuf;
  }

}

#ifdef HAVE_GPGME
bool
BodyPane ::get_gpgsig_from_gmime_part (GMimePart * part)
{
  GMimeDataWrapper * wrapper (g_mime_part_get_content_object (part));
  GMimeStream * mem_stream (g_mime_stream_mem_new ());
  if (wrapper)
  {
    g_mime_data_wrapper_write_to_stream (wrapper, mem_stream);
    g_mime_stream_reset(mem_stream);
    gpg_decrypt_and_verify(_signer_info, _gpgerr, mem_stream);
    return true;
  }
  return false;
}
#endif

void
BodyPane :: append_part (GMimeObject * obj, GtkAllocation * widget_size)
{
  bool is_done (false);

  // we only need leaf parts..
  if (!GMIME_IS_PART (obj))
    return;

  GMimePart * part = GMIME_PART (obj);
  GMimeContentType * type = g_mime_object_get_content_type (GMIME_OBJECT (part));

  // decide whether or not this part is a picture
  bool is_image (g_mime_content_type_is_type (type, "image", "*"));
  if (!is_image && g_mime_content_type_is_type(type, "application", "octet-stream")) {
    const char *type, *subtype;
    mime::guess_part_type_from_filename(g_mime_part_get_filename(part), &type, &subtype);
    is_image = type && !strcmp(type,"image");
  }

  // if it's a picture, draw it
  if (is_image)
  {
    GdkPixbuf * original (get_pixbuf_from_gmime_part (part));
    const bool fullsize (!_prefs.get_flag ("size-pictures-to-fit", true));
    GdkPixbuf * scaled (size_to_fit (original, fullsize ? 0 : widget_size));

    if (scaled != 0)
    {
      GtkTextIter iter;

      // if this is the first thing in the buffer, precede it with a linefeed.
      gtk_text_buffer_get_end_iter (_buffer, &iter);
      if (gtk_text_buffer_get_char_count (_buffer) > 0)
        gtk_text_buffer_insert (_buffer, &iter, "\n", -1);

      // rembember the location of the first picture.
      if (gtk_text_buffer_get_mark (_buffer, FIRST_PICTURE) == NULL)
        gtk_text_buffer_create_mark (_buffer, FIRST_PICTURE, &iter, true);
        gtk_text_buffer_create_mark (_buffer, FIRST_PICTURE, &iter, true);

      // add the picture
      const int begin_offset (gtk_text_iter_get_offset (&iter));
      gtk_text_buffer_insert_pixbuf (_buffer, &iter, scaled);
      gtk_text_buffer_insert (_buffer, &iter, "\n", -1);
      GtkTextIter iter_begin (iter);
      gtk_text_iter_set_offset (&iter_begin, begin_offset);

      // hook onto the tag a reference to the original picture
      // so that we can resize it later if user resizes the text pane.
      set_pixbuf_at_offset (_buffer, begin_offset, original, scaled);
      set_fullsize_flag (_buffer, fullsize);

      GtkTextTagTable * tags (gtk_text_buffer_get_tag_table (_buffer));
      GtkTextTag * tag (gtk_text_tag_table_lookup (tags, "pixbuf"));
      gtk_text_buffer_apply_tag (_buffer, tag, &iter_begin, &iter);

      g_object_unref (scaled);
      g_object_unref (original);

      is_done = true;
    }
  }

  // or, if it's text, display it
  else if (g_mime_content_type_is_type (type, "text", "*") ||
           (g_mime_content_type_is_type (type, "*", "pgp-signature")))
  {
    const char * fallback_charset (_charset.c_str());
    const char * p_flowed (g_mime_object_get_content_type_parameter(obj,"format"));
    const bool flowed (g_strcmp0 (p_flowed, "flowed") == 0);
    std::string str = mime_part_to_utf8 (part, fallback_charset);

    if (!str.empty() && _prefs.get_flag ("wrap-article-body", false))
      str = _tm.fill (str, flowed);

    const bool do_mute (_prefs.get_flag ("mute-quoted-text", false));
    const bool do_smilies (_prefs.get_flag ("show-smilies-as-graphics", true));
    const bool do_markup (_prefs.get_flag ("show-text-markup", true));
    const bool do_urls (_prefs.get_flag ("highlight-urls", true));
    append_text_buffer_nolock (&_tm, _buffer, str, do_mute, do_smilies, do_markup, do_urls);
    is_done = true;
#ifdef HAVE_GPGME
    /* verify signature */
    if (g_mime_content_type_is_type (type, "*", "pgp-signature"))
    {
      bool res = get_gpgsig_from_gmime_part(part);
      std::cerr<<"1023\n";
      if (res) update_sig_valid(_gpgerr.verify_ok);
    }
#endif
  }

  // otherwise, bitch and moan.
  if (!is_done) {
    const char * filename = g_mime_part_get_filename (part);
    char * pch = (filename && *filename)
      ? g_strdup_printf (_("Attachment not shown: MIME type %s/%s; filename %s\n"), type->type, type->subtype, filename)
      : g_strdup_printf (_("Attachment not shown: MIME type %s/%s\n"), type->type, type->subtype);
    GtkTextIter iter;
    gtk_text_buffer_get_end_iter (_buffer, &iter);
    gtk_text_buffer_insert (_buffer, &iter, pch, -1);
    g_free (pch);
  }
}

void
BodyPane :: foreach_part_cb (GMimeObject* /*parent*/, GMimeObject* o, gpointer self)
{
  BodyPane * pane = static_cast<BodyPane*>(self);
  GtkWidget * w (pane->_text);
  GtkAllocation aloc;
  gtk_widget_get_allocation(w, &aloc);
  pane->append_part (o, &aloc);
}


/***
****  HEADERS
***/
namespace
{
  void add_bold_header_value (std::string   & s,
                              GMimeMessage  * message,
                              const char    * key,
                              const char    * fallback_charset)
  {
    const char * val (message ? g_mime_object_get_header ((GMimeObject *)message, key) : "");
    const std::string utf8_val (header_to_utf8 (val, fallback_charset));
    char * e (0);
    if (strcmp (key, "From"))
      e = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", utf8_val.c_str());
    else {
      const StringView v = GNKSA :: get_short_author_name (utf8_val);
      e = g_markup_printf_escaped ("<span weight=\"bold\">%s</span>", v.to_string().c_str());
    }
    s += e;
    g_free (e);
  }

  size_t add_header_line (std::string  & s,
                          const char   * key_i18n,
                          const char   * key,
                          const char   * val,
                          const char   * fallback_charset)
  {
    char * e;
    e = g_markup_printf_escaped ("<span weight=\"bold\">%s:</span> ", key_i18n);
    s += e;
    g_free (e);
    const std::string utf8_val (header_to_utf8 (val, fallback_charset));
    e = g_markup_printf_escaped ("%s\n", utf8_val.c_str());
    s += e;
    const size_t retval (g_utf8_strlen(key,-1) + g_utf8_strlen(utf8_val.c_str(),-1) + 2);
    g_free (e);
    return retval;
  }

  size_t add_header_line (std::string   & s,
                          GMimeMessage  * msg,
                          const char    * key_i18n,
                          const char    * key,
                          const char    * fallback_charset)
  {
    const char * val (msg ? g_mime_object_get_header ((GMimeObject *) msg, key) : "");
    return add_header_line (s, key_i18n, key, val, fallback_charset);
  }

}

void
BodyPane :: set_text_from_message (GMimeMessage * message)
{
  const char * fallback_charset (_charset.empty() ? 0 : _charset.c_str());

  // mandatory headers...
  std::string s;
  size_t w(0), l(0);
  l = add_header_line (s, message, _("Subject"), "Subject", fallback_charset);
  w = std::max (w, l);
  l = add_header_line (s, message, _("From"), "From", fallback_charset);
  w = std::max (w, l);
  l = add_header_line (s, message, _("Date"), "Date", fallback_charset);
  w = std::max (w, l);

  // conditional headers...
  if (message) {
    const StringView newsgroups (g_mime_object_get_header ((GMimeObject *) message, "Newsgroups"));
    if (newsgroups.strchr(',')) {
      l = add_header_line (s, message, _("Newsgroups"), "Newsgroups", fallback_charset);
      w = std::max (w, l);
    }
    const StringView followup_to (g_mime_object_get_header ((GMimeObject *) message, "Followup-To"));
    if (!followup_to.empty() && (followup_to!=newsgroups)) {
      l = add_header_line (s, message, _("Followup-To"), "Followup-To", fallback_charset);
      w = std::max (w, l);
    }
    const StringView reply_to (g_mime_object_get_header ((GMimeObject *) message, "Reply-To"));
    if (!reply_to.empty()) {
      const StringView from (g_mime_object_get_header ((GMimeObject *) message, "From"));
      StringView f_addr, f_name, rt_addr, rt_name;
      GNKSA :: do_check_from (from, f_addr, f_name, false);
      GNKSA :: do_check_from (reply_to, rt_addr, rt_name, false);
      if (f_addr != rt_addr) {
        l = add_header_line (s, message, _("Reply-To"), "Reply-To", fallback_charset);
        w = std::max (w, l);
      }
    }

      // obsolete in favor of the certificate icon tooltip (bodypane)
//    const StringView gpg (g_mime_object_get_header ((GMimeObject *) message, "X-GPG-Signed"));
//    if (!gpg.empty())
//    {
//      char buf[256];
//      l = add_header_line (s, message, _("GPG-Signed message signature "), "X-GPG-Signed", fallback_charset);
//      w = std::max (w, l);
//    }
  }

  s.resize (s.size()-1); // remove trailing linefeed
  gtk_label_set_markup (GTK_LABEL(_headers), s.c_str());

  // ellipsize mode is useless w/o this in expander...
  gtk_label_set_width_chars (GTK_LABEL(_headers), (int)w);

  // set the x-face...
  gtk_image_clear(GTK_IMAGE(_xface));
  const char * pch = message ? g_mime_object_get_header ((GMimeObject *) message, "X-Face") : 0;
  if (pch && gtk_widget_get_window(_xface) )
  {
    GdkPixbuf *pixbuf = NULL;
    pixbuf = pan_gdk_pixbuf_create_from_x_face (pch);
    gtk_image_set_from_pixbuf (GTK_IMAGE(_xface), pixbuf);
    g_object_unref (pixbuf);
  }
  // set the face
  gtk_image_clear(GTK_IMAGE(_face));
  pch = message ? g_mime_object_get_header ((GMimeObject *) message, "Face") : 0;
  if (pch && gtk_widget_get_window(_face))
  {
    GMimeEncoding dec;
    g_mime_encoding_init_decode(&dec, GMIME_CONTENT_ENCODING_BASE64);
    guchar buf[1024];
    int len = g_mime_encoding_step(&dec, pch, strlen(pch), (char*)buf);
    GdkPixbufLoader *pl = gdk_pixbuf_loader_new_with_type( "png", NULL);
    gdk_pixbuf_loader_write(pl, buf, len, NULL);
    gdk_pixbuf_loader_close(pl, NULL);
    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(pl);
    gtk_image_set_from_pixbuf (GTK_IMAGE(_face), pixbuf);
    g_object_unref(pl);
  }

  // set the terse headers...
  s.clear ();
  add_bold_header_value (s, message, "Subject", fallback_charset);
  s += _(" from ");
  add_bold_header_value (s, message, "From", fallback_charset);
  s += _(" at ");
  add_bold_header_value (s, message, "Date", fallback_charset);
  gtk_label_set_markup (GTK_LABEL(_terse), s.c_str());
  // ellipsize mode is useless w/o this in expander...
  gtk_label_set_width_chars (GTK_LABEL(_terse), (int)s.size());

  // clear the text buffer...
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (_buffer, &start, &end);
  gtk_text_buffer_delete (_buffer, &start, &end);
  if (gtk_text_buffer_get_mark (_buffer, FIRST_PICTURE) != NULL)
    gtk_text_buffer_delete_mark_by_name (_buffer, FIRST_PICTURE);
  clear_pixbuf_cache (_buffer);

  // maybe add the headers
  const bool do_show_headers (_prefs.get_flag ("show-all-headers", false));
  if (message && do_show_headers) {
    char * headers (g_mime_object_get_headers ((GMimeObject *) message));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter (_buffer, &end);
    StringView line, v(headers);
    while (v.pop_token (line, '\n')) {
      const std::string h (header_to_utf8 (line, fallback_charset));
      gtk_text_buffer_insert (_buffer, &end, h.c_str(), h.size());
      gtk_text_buffer_insert (_buffer, &end, "\n", 1);
    }
    gtk_text_buffer_insert (_buffer, &end, "\n", 1);
    g_free (headers);
  }

  // FIXME: need to set a mark here so that when user hits follow-up,
  // the all-headers don't get included in the followup

  // set the text buffer...
  if (message)
    g_mime_message_foreach (message, foreach_part_cb, this);

  // if there was a picture, scroll to it.
  // otherwise scroll to the top of the body.
  GtkTextMark * mark = gtk_text_buffer_get_mark (_buffer, FIRST_PICTURE);
  if (mark && _prefs.get_flag ("focus-on-image", true))
    gtk_text_view_scroll_to_mark (GTK_TEXT_VIEW(_text), mark, 0.0, true, 0.0, 0.0);
  else {
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter  (_buffer, &iter);
    gtk_text_view_scroll_to_iter (GTK_TEXT_VIEW(_text), &iter, 0.0, true, 0.0, 0.0);
  }
  std::cerr<<"1240\n";

}

void
BodyPane :: refresh ()
{
  set_text_from_message (_message);
}

namespace
{

  std::string get_email_address(std::string& s)
  {
    size_t in = s.find("<");
    size_t out = s.find(">");
    if (in == std::string::npos || out == std::string::npos) return "...";

    return s.substr (in+1,out-in-1);
  }
}

#ifdef HAVE_GPGME
gboolean
BodyPane:: on_tooltip_query(GtkWidget  *widget,
                            gint        x,
                            gint        y,
                            gboolean    keyboard_tip,
                            GtkTooltip *tooltip,
                            gpointer    data)
{
  BodyPane* pane = static_cast<BodyPane*>(data);
  GPGDecErr& err = pane->_gpgerr;
  GPGSignersInfo& info = pane->_signer_info;

  g_return_val_if_fail(err.dec_ok, false);
  g_return_val_if_fail(err.err == GPG_ERR_NO_ERROR || err.err == GPG_ERR_NO_DATA, false);

  if (err.no_sigs) return false;
  if (!err.v_res) return false;
  if (!err.v_res->signatures) return false;
  if (!err.v_res->signatures->fpr) return false;

// get uid from fingerprint
//  GPGSignersInfo info = get_uids_from_fingerprint(err->v_res->signatures->fpr);

  EvolutionDateMaker ed;

  char buf[2048];
  g_snprintf(buf, sizeof(buf),
             "<u>This is a <b>GPG-Signed</b> message.</u>\n\n"
             "<b>Signer</b> : %s (\"%s\")\n"
             "<b>Valid until</b> : %s\n"
             "<b>Created on</b> : %s",
             info.real_name.c_str(), get_email_address(info.uid).c_str(),
             ed.get_date_string(info.expires),
             ed.get_date_string(info.creation_timestamp)
             );

  gtk_tooltip_set_icon_from_stock (tooltip, GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_DIALOG);
  gtk_tooltip_set_markup (tooltip, buf);

  return true;
}


void
BodyPane :: update_sig_valid(int i)
{

  std::cerr<<"update sig "<<i<<"\n";

  gtk_image_clear(GTK_IMAGE(_sig_icon));

  switch (i)
  {
    case 0:
      gtk_image_set_from_pixbuf (GTK_IMAGE(_sig_icon), icons[ICON_SIG_FAIL].pixbuf);
      break;

    case 1:
      gtk_image_set_from_pixbuf (GTK_IMAGE(_sig_icon), icons[ICON_SIG_OK].pixbuf);
      break;
  }
}
#endif

void
BodyPane :: set_article (const Article& a)
{
  _article = a;

  const char* gpg_sign(0);

  if (_message)
    g_object_unref (_message);
#ifdef HAVE_GPGME
  _message = _cache.get_message (_article.get_part_mids(), _signer_info, _gpgerr);
  gpg_sign = g_mime_object_get_header(GMIME_OBJECT(_message), "X-GPG-Signed");
#else
  _message = _cache.get_message (_article.get_part_mids());
#endif

  int val(-1);
  if (gpg_sign)
  {
    if (!strcmp(gpg_sign, "valid"))
      val = 1;
    else if (!strcmp(gpg_sign, "invalid"))
      val = 0;
  }
#ifdef HAVE_GPGME
  std::cerr<<"1354\n";
  update_sig_valid(val);
#endif
  refresh ();

  _data.mark_read (_article);
}

void
BodyPane :: clear ()
{
  if (_message)
    g_object_unref (_message);
  _message = 0;
  refresh ();
#ifdef HAVE_GPGME
  update_sig_valid(-1);
#endif
}

void
BodyPane :: select_all ()
{
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds (_buffer, &start, &end);
  gtk_text_buffer_select_range (_buffer, &start, &end);
}

void
BodyPane :: rot13_selected_text ()
{
  GtkTextIter start, end;
  if (gtk_text_buffer_get_selection_bounds (_buffer, &start, &end))
  {
    // replace the range with a rot13'ed copy.
    gchar* pch = gtk_text_buffer_get_text (_buffer, &start, &end, false);
    const size_t len = strlen (pch);
    TextMassager :: rot13_inplace (pch);
    gtk_text_buffer_delete (_buffer, &start, &end);
    gtk_text_buffer_insert (_buffer, &end, pch, len);
    g_free (pch);

    // resync selection.
    // since gtk_text_buffer_insert() invalided start, we rebuild it first.
    start = end;
    gtk_text_iter_backward_chars (&start, len);
    gtk_text_buffer_select_range (_buffer, &start, &end);
  }
}

/***
****
***/

gboolean
BodyPane :: expander_activated_idle (gpointer self_gpointer)
{
  BodyPane *  self (static_cast<BodyPane*>(self_gpointer));
  GtkExpander * ex (GTK_EXPANDER(self->_expander));
  const bool expanded = gtk_expander_get_expanded (ex);
  gtk_expander_set_label_widget (ex, expanded ? self->_verbose : self->_terse);
  self->_prefs.set_flag ("body-pane-headers-expanded", expanded);
  return false;
}
void
BodyPane :: expander_activated_cb (GtkExpander*, gpointer self_gpointer)
{
  g_idle_add (expander_activated_idle, self_gpointer);
}

void
BodyPane :: refresh_scroll_visible_state ()
{
  GtkScrolledWindow * w (GTK_SCROLLED_WINDOW (_scroll));
  GtkAdjustment * adj = gtk_scrolled_window_get_hadjustment (w);
  _hscroll_visible = gtk_adjustment_get_page_size(adj) < gtk_adjustment_get_upper(adj);
  adj = gtk_scrolled_window_get_vadjustment (w);
  _vscroll_visible = gtk_adjustment_get_page_size(adj) < gtk_adjustment_get_upper(adj);
}

// show*_cb exist to ensure that _hscroll_visible and _vscroll_visible
// are initialized properly when the body pane becomes shown onscreen.

gboolean
BodyPane :: show_idle_cb (gpointer pane)
{
  static_cast<BodyPane*>(pane)->refresh_scroll_visible_state ();
  return false;
}
void
BodyPane :: show_cb (GtkWidget* w G_GNUC_UNUSED, gpointer pane)
{
  g_idle_add (show_idle_cb, pane);
}

namespace
{
  guint text_size_allocated_idle_tag (0);
}

gboolean
BodyPane :: text_size_allocated_idle_cb (gpointer pane)
{
  static_cast<BodyPane*>(pane)->text_size_allocated_idle ();
  text_size_allocated_idle_tag = 0;
  return false;
}

void
BodyPane :: text_size_allocated_idle ()
{
  // prevent oscillation
  const bool old_h (_hscroll_visible);
  const bool old_v (_vscroll_visible);
  refresh_scroll_visible_state ();
  if ((old_h!=_hscroll_visible) || (old_v!=_vscroll_visible))
    return;

  // get the resize flag...
  GtkTextBuffer * buf (gtk_text_view_get_buffer (GTK_TEXT_VIEW(_text)));
  const bool fullsize (get_fullsize_flag (buf));

  // get the start point...
  GtkTextIter iter;
  gtk_text_buffer_get_start_iter (buf, &iter);

  // walk through the buffer looking for pictures to resize
  GtkTextTag * tag (get_named_tag_from_view (_text, "pixbuf"));
  GtkAllocation aloc;
  for (;;) {
    if (gtk_text_iter_begins_tag (&iter, tag))
    {
      gtk_widget_get_allocation(_text, &aloc);
      resize_picture_at_iter (buf, &iter, fullsize, &aloc, tag);
    }
    if (!gtk_text_iter_forward_char (&iter))
      break;
    if (!gtk_text_iter_forward_to_tag_toggle (&iter, tag))
      break;
  }
}

void
BodyPane :: text_size_allocated (GtkWidget     * text        G_GNUC_UNUSED,
                                 GtkAllocation * allocation  G_GNUC_UNUSED,
                                 gpointer        pane)
{
  if (!text_size_allocated_idle_tag)
       text_size_allocated_idle_tag = g_idle_add (text_size_allocated_idle_cb, pane);
}

/***
****
***/

void
BodyPane :: copy_url_cb (GtkMenuItem *mi G_GNUC_UNUSED, gpointer pane)
{
  static_cast<BodyPane*>(pane)->copy_url ();
}
void
BodyPane :: copy_url ()
{
  gtk_clipboard_set_text (gtk_clipboard_get (GDK_SELECTION_CLIPBOARD),
                          _hover_url.c_str(), _hover_url.size());
}


void
BodyPane :: populate_popup_cb (GtkTextView *v, GtkMenu *m, gpointer pane)
{
  static_cast<BodyPane*>(pane)->populate_popup(v, m);
}
void
BodyPane :: populate_popup (GtkTextView *v G_GNUC_UNUSED, GtkMenu *m)
{
  // menu separator comes first.
  GtkWidget * mi = gtk_menu_item_new();
  gtk_widget_show (mi);
  gtk_menu_shell_prepend (GTK_MENU_SHELL(m), mi);

  // then, on top of it, the suggestions menu.
  const bool copy_url_enabled = !_hover_url.empty();
  GtkWidget * img = gtk_image_new_from_stock (GTK_STOCK_COPY, GTK_ICON_SIZE_MENU);
  mi = gtk_image_menu_item_new_with_mnemonic (_("Copy _URL"));
  g_signal_connect (mi, "activate", G_CALLBACK(copy_url_cb), this);
  gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), img);
  gtk_widget_set_sensitive (mi, copy_url_enabled);
  gtk_widget_show_all (mi);
  gtk_menu_shell_prepend (GTK_MENU_SHELL(m), mi);
}

/***
****
***/

BodyPane :: BodyPane (Data& data, ArticleCache& cache, Prefs& prefs):
  _prefs (prefs),
  _data (data),
  _cache (cache),
  _hscroll_visible (false),
  _vscroll_visible (false),
  _message (0)
{

  for (guint i=0; i<NUM_ICONS; ++i)
    icons[i].pixbuf = gdk_pixbuf_new_from_inline (-1, icons[i].pixbuf_txt, FALSE, 0);

  _sig_icon = gtk_image_new();

  GtkWidget * vbox = gtk_vbox_new (false, PAD);
  gtk_container_set_resize_mode (GTK_CONTAINER(vbox), GTK_RESIZE_QUEUE);

  // about this expander... getting the ellipsis to work is a strange process.
  // once you turn ellipsize on, the expander tries to make its label as narrow
  // as it can and just have the three "..."s.  So gtk_label_set_width_chars
  // is used to force labels to want to be the right size... but then they
  // never ellipsize.  But, if we start with gtk_widget_set_size_request() to
  // tell the expander that it _wants_ to be very small, then it will still take
  // extra space given to it by its parent without asking for enough size to
  // fit the entire label.
  GtkWidget * w = _expander = gtk_expander_new (NULL);
  gtk_widget_set_size_request (w, 50, -1);
  g_signal_connect (w, "activate", G_CALLBACK(expander_activated_cb), this);
  gtk_box_pack_start (GTK_BOX(vbox), w, false, false, 0);

  _terse = gtk_label_new ("Expander");
  g_object_ref_sink (G_OBJECT(_terse));
  gtk_misc_set_alignment (GTK_MISC(_terse), 0.0f, 0.5f);
  gtk_label_set_use_markup (GTK_LABEL(_terse), true);
  gtk_label_set_selectable (GTK_LABEL(_terse), true);
  gtk_label_set_ellipsize (GTK_LABEL(_terse), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_show (_terse);

  GtkWidget * hbox = _verbose = gtk_hbox_new (false, 0);
  g_object_ref_sink (G_OBJECT(_verbose));
  w = _headers = gtk_label_new ("Headers");
  gtk_label_set_selectable (GTK_LABEL(_headers), TRUE);
  gtk_misc_set_alignment (GTK_MISC(w), 0.0f, 0.5f);
  gtk_label_set_ellipsize (GTK_LABEL(w), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_use_markup (GTK_LABEL(w), true);
  gtk_box_pack_start (GTK_BOX(hbox), w, true, true, PAD_SMALL);
#ifdef HAVE_GPGME
  gtk_widget_set_size_request (_sig_icon, 32, 32);
  gtk_box_pack_start (GTK_BOX(hbox), _sig_icon, true, true, PAD_SMALL);
  gtk_widget_set_has_tooltip (_sig_icon, true);
  g_signal_connect(_sig_icon,"query-tooltip",G_CALLBACK(on_tooltip_query), this);
#endif
  w = _xface = gtk_image_new();
  gtk_widget_set_size_request (w, 48, 48);
  gtk_box_pack_start (GTK_BOX(hbox), w, false, false, PAD_SMALL);
  w = _face = gtk_image_new ();
  gtk_widget_set_size_request (w, 48, 48);
  gtk_box_pack_start (GTK_BOX(hbox), w, false, false, PAD_SMALL);
  gtk_widget_show_all (_verbose);

  // setup
  _text = gtk_text_view_new ();
  refresh_fonts ();
  gtk_widget_add_events (_text, GDK_POINTER_MOTION_MASK|GDK_POINTER_MOTION_HINT_MASK);
  gtk_container_set_border_width (GTK_CONTAINER(_text), PAD_SMALL);
  gtk_text_view_set_editable (GTK_TEXT_VIEW(_text), false);
  gtk_text_view_set_cursor_visible (GTK_TEXT_VIEW(_text), false);
  _scroll = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(_scroll), GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (_scroll),
                                  GTK_POLICY_AUTOMATIC,
                                  GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER(_scroll), _text);
  gtk_widget_show_all (vbox);
  gtk_box_pack_start (GTK_BOX(vbox), _scroll, true, true, 0);

  // set up the buffer tags
  _buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW(_text));
  set_text_buffer_tags (_buffer, _prefs);

  set_text_from_message (0);
  const bool expanded (_prefs.get_flag ("body-pane-headers-expanded", true));
  gtk_expander_set_expanded (GTK_EXPANDER(_expander), expanded);
  expander_activated_idle (this);

  _root = vbox;
  _prefs.add_listener (this);

  // listen for user interaction
  g_signal_connect (_text, "motion_notify_event", G_CALLBACK(motion_notify_event), &_hover_url);
  g_signal_connect (_text, "button_press_event", G_CALLBACK(mouse_button_pressed_cb), this);
  g_signal_connect (_text, "key_press_event", G_CALLBACK(text_key_pressed), _scroll);
  g_signal_connect (_text, "size_allocate", G_CALLBACK(text_size_allocated), this);
  g_signal_connect (_text, "populate_popup", G_CALLBACK(populate_popup_cb), this);
  g_signal_connect (_root, "show", G_CALLBACK(show_cb), this);

  gtk_widget_show_all (_root);
}

BodyPane :: ~BodyPane ()
{
  _prefs.remove_listener (this);

  g_object_unref (_verbose);
  g_object_unref (_terse);

  if (_message)
    g_object_unref (_message);

  for (int i=0; i<NUM_ICONS; ++i)
    g_object_unref (icons[i].pixbuf);
}


namespace
{
  const int smooth_scrolling_speed (10);

  void sylpheed_textview_smooth_scroll_do (GtkAdjustment  * vadj,
                                           gfloat           old_value,
                                           gfloat           new_value,
                                           int              step)
  {
    const bool down (old_value < new_value);
    const int change_value = (int)(down ? new_value-old_value : old_value-new_value);
    for (int i=step; i<=change_value; i+=step)
      gtk_adjustment_set_value (vadj, old_value+(down?i:-i));
    gtk_adjustment_set_value (vadj, new_value);
  }
}

bool
BodyPane :: read_more_or_less (bool more)
{
  GtkWidget * parent = gtk_widget_get_parent (_text);
  GtkAdjustment * v = gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW(parent));

  // figure out how far we scroll
  const double v_value = gtk_adjustment_get_value(v);
  const double v_upper = gtk_adjustment_get_upper(v);
  const double v_lower = gtk_adjustment_get_lower(v);
  const double v_page_size = gtk_adjustment_get_page_size(v);
  const int arbitrary_font_height_pixels_hack (18);
  const float inc (v_page_size - arbitrary_font_height_pixels_hack);
  const gfloat val (CLAMP (v_value + (more ? inc : -inc),
                           v_lower,
                           MAX(v_upper,v_page_size)-MIN(v_upper,v_page_size)));

  // if we can scroll, do so.
  bool handled (false);
  if (v_upper>=v_page_size && val!=v_value)
  {
    if (_prefs.get_flag ("smooth-scrolling", true))
      sylpheed_textview_smooth_scroll_do (v, v_value, val, smooth_scrolling_speed);
    else
      gtk_adjustment_set_value (v, val);

    handled = true;
  }

  return handled;
}

namespace
{
  // (1) strip redundant leading Re: and RE:
  // (2) ensure the remaining Re: has a lowercase e

  std::string normalize_subject_re (const StringView& v_in)
  {
    StringView v(v_in), prev(v_in);
    while (!v.empty()) {
      v.ltrim ();
      StringView tmp (v);
      if (tmp.strstr("Re:") == tmp.str)
        tmp.eat_chars (3);
      else if (v.strstr("RE:") == tmp.str)
        tmp.eat_chars (3);
      else
        break;
      prev = v;
      v = tmp;
    }

    std::string ret (prev.str, prev.len);
    if (!ret.find("RE:")) // force lowercase 'e'
      ret.replace (0, 3, "Re:");

    return ret;
  }

  std::string get_header (GMimeMessage * msg,
                          const char   * key,
                          const char   * fallback_charset_1,
                          const char   * fallback_charset_2)
  {
    const StringView v (g_mime_object_get_header ((GMimeObject *) msg, key));
    std::string s;
    if (!v.empty())
      s = header_to_utf8 (v, fallback_charset_1, fallback_charset_2);
    return s;
  }

  struct ForeachPartData
  {
    std::string fallback_charset;
    std::string body;
  };

  void get_utf8_body_foreach_part (GMimeObject* /*parent*/, GMimeObject *o,
                                   gpointer user_data)
  {
    GMimePart * part;
    GMimeContentType * type = g_mime_object_get_content_type (o);
    const bool is_text (g_mime_content_type_is_type (type, "text", "*"));
    if (is_text)
    {
      part = GMIME_PART (o);
      ForeachPartData *data (static_cast<ForeachPartData*>(user_data));
      data->body += mime_part_to_utf8 (part, data->fallback_charset.c_str());
    }
  }

  std::string get_utf8_body (GMimeMessage * source,
                             const char   * fallback_charset)
  {
    ForeachPartData tmp;
    if (fallback_charset)
      tmp.fallback_charset = fallback_charset;
    if (source)
      g_mime_message_foreach (source, get_utf8_body_foreach_part, &tmp);
    return tmp.body;
  }
}

GMimeMessage*
BodyPane :: create_followup_or_reply (bool is_reply)
{
  GMimeMessage * msg (0);

  if (_message)
  {
    msg = g_mime_message_new (false);
    GMimeObject *msg_obj = (GMimeObject*)msg;
    GMimeObject *_message_obj = (GMimeObject*)_message;

    // fallback character encodings
    const char * group_charset (_charset.c_str());
    GMimeContentType * type (g_mime_object_get_content_type (GMIME_OBJECT(_message)));
    const char * message_charset (type ? g_mime_content_type_get_parameter (type, "charset") : 0);

    ///
    ///  HEADERS
    ///

    // To:, Newsgroups:
    const std::string from       (get_header (_message, "From",        message_charset, group_charset));
    const std::string newsgroups (get_header (_message, "Newsgroups",  message_charset, group_charset));
    const std::string fup_to     (get_header (_message, "Followup-To", message_charset, group_charset));
    const std::string reply_to   (get_header (_message, "Reply-To",    message_charset, group_charset));

    if (is_reply || fup_to=="poster") {
      const std::string& to (reply_to.empty() ? from : reply_to);
      pan_g_mime_message_add_recipients_from_string (msg, GMIME_RECIPIENT_TYPE_TO, to.c_str());
    } else {
      const std::string& groups (fup_to.empty() ? newsgroups : fup_to);
      g_mime_object_append_header ((GMimeObject *) msg, "Newsgroups", groups.c_str());
    }

    // Subject:
    StringView v = g_mime_object_get_header (_message_obj, "Subject");
    std::string h = header_to_utf8 (v, message_charset, group_charset);
    std::string val (normalize_subject_re (h));
    if (val.find ("Re:") != 0) // add "Re: " if we don't have one
      val.insert (0, "Re: ");
    g_mime_message_set_subject (msg, val.c_str());

    // attribution lines
    const char * cpch = g_mime_object_get_header (_message_obj, "From");
    h = header_to_utf8 (cpch, message_charset, group_charset);
    g_mime_object_append_header (msg_obj, "X-Draft-Attribution-Author", h.c_str());

    cpch = g_mime_message_get_message_id (_message);
    h = header_to_utf8 (cpch, message_charset, group_charset);
    g_mime_object_append_header (msg_obj, "X-Draft-Attribution-Id", h.c_str());

    char * tmp = g_mime_message_get_date_as_string (_message);
    h = header_to_utf8 (tmp, message_charset, group_charset);
    g_mime_object_append_header (msg_obj, "X-Draft-Attribution-Date", h.c_str());
    g_free (tmp);

    // references
    const char * header = "References";
    v = g_mime_object_get_header (_message_obj, header);
    val.assign (v.str, v.len);
    if (!val.empty())
      val += ' ';
    val += "<";
    val += g_mime_message_get_message_id (_message);
    val += ">";
    val = GNKSA :: trim_references (val);
    g_mime_object_append_header (msg_obj, header, val.c_str());

    ///
    ///  BODY
    ///

    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds (_buffer, &start, &end))
    {
      // go with the user-selected region w/o modifications.
      // since it's in the text pane it's already utf-8...
      h = gtk_text_buffer_get_text (_buffer, &start, &end, false);
    }
    else
    {
      // get the body; remove the sig
      h = get_utf8_body (_message, group_charset);
      StringView v (h);
      int sig_index (0);
      if (GNKSA::find_signature_delimiter (h, sig_index) != GNKSA::SIG_NONE)
        v.len = sig_index;
      v.trim ();
      h = std::string (v.str, v.len);
    }

    // quote the body
    std::string s;
    for (const char *c(h.c_str()); c && *c; ++c) {
      if (c==h.c_str() || c[-1]=='\n')
        s += (*c=='>' ? ">" : "> ");
      s += *c;
    }

    // set the clone's content object with our modified body
    GMimeStream * stream = g_mime_stream_mem_new ();
    g_mime_stream_write_string (stream, s.c_str());
    GMimeDataWrapper * wrapper = g_mime_data_wrapper_new_with_stream (stream, GMIME_CONTENT_ENCODING_8BIT);
    GMimePart * part = g_mime_part_new ();
    GMimeContentType * new_type = g_mime_content_type_new_from_string ("text/plain; charset=UTF-8");
    g_mime_object_set_content_type ((GMimeObject *) part, new_type);
    g_mime_part_set_content_object (part, wrapper);
    g_mime_part_set_content_encoding (part, GMIME_CONTENT_ENCODING_8BIT);
    g_mime_message_set_mime_part (msg, GMIME_OBJECT(part));
    g_object_unref (new_type);
    g_object_unref (wrapper);
    g_object_unref (part);
    g_object_unref (stream);
//std::cerr << LINE_ID << " here is the modified clone\n [" << g_mime_object_to_string((GMimeObject *)msg) << ']' << std::endl;
  }

  return msg;
}

/***
****
***/

void
BodyPane :: refresh_fonts ()
{
  const bool body_pane_font_enabled = _prefs.get_flag ("body-pane-font-enabled", false);
  const bool monospace_font_enabled = _prefs.get_flag ("monospace-font-enabled", false);

  if (!body_pane_font_enabled && !monospace_font_enabled)
    gtk_widget_override_font (_text, 0);
  else {
    const std::string str (monospace_font_enabled
      ? _prefs.get_string ("monospace-font", "Monospace 10")
      : _prefs.get_string ("body-pane-font", "Sans 10"));
    PangoFontDescription * pfd (pango_font_description_from_string (str.c_str()));
    gtk_widget_override_font (_text, pfd);
    pango_font_description_free (pfd);
  }
}

void
BodyPane :: on_prefs_flag_changed (const StringView& key, bool value G_GNUC_UNUSED)
{
  if ((key=="body-pane-font-enabled") || (key=="monospace-font-enabled"))
    refresh_fonts ();

  if ((key=="wrap-article-body") || (key=="mute-quoted-text") ||
      (key=="show-smilies-as-graphics") || (key=="show-all-headers") ||
      (key=="size-pictures-to-fit") || (key=="show-text-markup") ||
      (key=="highlight-urls") )
    refresh ();
}

void
BodyPane :: on_prefs_string_changed (const StringView& key, const StringView& value G_GNUC_UNUSED)
{
  if ((key=="body-pane-font") || (key=="monospace-font"))
    refresh_fonts ();

}

void
BodyPane :: on_prefs_color_changed (const StringView& key, const GdkColor& color G_GNUC_UNUSED)
{
  if (key.strstr ("body-pane-color") != 0)
    refresh_colors ();
}

void
BodyPane :: refresh_colors ()
{
  set_text_buffer_tags (_buffer, _prefs);
  set_text_from_message (_message);
}

void
BodyPane :: set_character_encoding (const char * charset)
{
  if (charset && *charset)
    _charset = charset;
  else
    _charset.clear ();

  refresh ();
}
