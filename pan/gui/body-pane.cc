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

#include "body-pane.h"

#include "pad.h"
#include "pan/gui/load-icon.h"
#include "save-attach-ui.h"
#include "tango-colors.h"
#include "url.h"
#include "xface.h"
#include <cctype>
#include <cmath>
#include <config.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdk/gdk.h>
#include <glib-object.h>
#include <glib/gi18n.h>
#include <gmime/gmime.h>
#include <gtk/gtk.h>
#include <iostream>
#include <pan/general/debug.h>
#include <pan/general/e-util.h>
#include <pan/general/log.h>
#include <pan/general/macros.h>
#include <pan/general/utf8-utils.h>
#include <pan/usenet-utils/gnksa.h>
#include <pan/usenet-utils/mime-utils.h>
#include <pan/usenet-utils/url-find.h>
#include <sstream>

#ifdef HAVE_WEBKIT
#include <webkit/webkit.h>
#endif

#define FIRST_PICTURE "first-picture"

#define FACE_SIZE 48

namespace pan {

/***
****
***/

namespace {

GtkWindow *get_window(GtkWidget *w)
{
  return GTK_WINDOW(gtk_widget_get_toplevel(w));
}

enum Icons
{
  ICON_SIG_OK,
  ICON_SIG_FAIL,
  NUM_ICONS
};

struct Icon
{
    char const *pixbuf_file;
    GdkPixbuf *pixbuf;
} icons[NUM_ICONS] = {{"icon_sig_ok.png", nullptr},
                      {"icon_sig_fail.png", nullptr}};
} // namespace

/***
****
***/

/**
***  Pixbuf Cache
**/
namespace {
class PixbufCache
{
  private:
    typedef std::pair<GdkPixbuf *, GdkPixbuf *>
      pixbuf_pair_t; // original, scaled
    typedef std::map<int, pixbuf_pair_t> pixbufs_t;
    pixbufs_t _pixbufs;

  public:
    PixbufCache()
    {
    }

    ~PixbufCache()
    {
      clear();
    }

  public:
    void clear()
    {
      foreach (pixbufs_t, _pixbufs, it)
      {
        g_object_unref(it->second.first);
        g_object_unref(it->second.second);
      }
      _pixbufs.clear();
    }

  public:
    bool get_pixbuf_at_offset(int offset,
                              GdkPixbuf *&original,
                              GdkPixbuf *&scaled)
    {
      if (! _pixbufs.count(offset))
      {
        return false;
      }
      pixbuf_pair_t &p(_pixbufs[offset]);
      original = p.first;
      scaled = p.second;
      return true;
    }

    void set_pixbuf_at_offset(int offset,
                              GdkPixbuf *original,
                              GdkPixbuf *scaled)
    {
      bool const had_old(_pixbufs.count(offset));
      pixbuf_pair_t &p(_pixbufs[offset]), old(p);
      g_object_ref(p.first = original);
      g_object_ref(p.second = scaled);
      if (had_old)
      {
        g_object_unref(old.first);
        g_object_unref(old.second);
      }
    }
};

void delete_pixbuf_cache(gpointer pc)
{
  delete static_cast<PixbufCache *>(pc);
}

PixbufCache &get_pixbuf_cache(gpointer gp)
{
  static char const *PIXBUF_CACHE("pixbuf-cache");
  GObject *o(G_OBJECT(gp));
  PixbufCache *pc((PixbufCache *)g_object_get_data(o, PIXBUF_CACHE));
  if (! pc)
  {
    pc = new PixbufCache();
    g_object_set_data_full(o, PIXBUF_CACHE, pc, delete_pixbuf_cache);
  }
  return *pc;
}

bool get_pixbuf_at_offset(gpointer o,
                          int offset,
                          GdkPixbuf *&original,
                          GdkPixbuf *&scaled)
{
  return get_pixbuf_cache(o).get_pixbuf_at_offset(offset, original, scaled);
}

void set_pixbuf_at_offset(gpointer o,
                          int offset,
                          GdkPixbuf *original,
                          GdkPixbuf *scaled)
{
  get_pixbuf_cache(o).set_pixbuf_at_offset(offset, original, scaled);
}

void clear_pixbuf_cache(gpointer o)
{
  get_pixbuf_cache(o).clear();
}
} // namespace

/**
***  "fullsize" flag
**/
namespace {
#define FULLSIZE "fullsize"

void set_fullsize_flag(gpointer o, bool b)
{
  g_object_set_data(G_OBJECT(o), FULLSIZE, GINT_TO_POINTER(b));
}

bool get_fullsize_flag(gpointer o)
{
  return g_object_get_data(G_OBJECT(o), FULLSIZE) != nullptr;
}

bool toggle_fullsize_flag(gpointer o)
{
  bool const b(! get_fullsize_flag(o));
  set_fullsize_flag(o, b);
  return b;
}
} // namespace

/**
***  Cursors
**/
namespace {
enum
{
  CURSOR_IBEAM,
  CURSOR_HREF,
  CURSOR_ZOOM_IN,
  CURSOR_ZOOM_OUT,
  CURSOR_QTY
};

GdkCursor **cursors(nullptr);
GdkCursor *cursor_current(nullptr);

void free_cursors(void)
{
  delete[] cursors;
}

void create_cursors()
{
  cursors = new GdkCursor *[CURSOR_QTY];
}

void init_cursors(GtkWidget *w)
{
  GdkDisplay *display(gtk_widget_get_display(w));

  int width, height;
  GtkIconSize const size(GTK_ICON_SIZE_LARGE_TOOLBAR);
  GdkPixbuf *pixbuf = gtk_widget_render_icon_pixbuf(w, GTK_STOCK_ZOOM_IN, size);
  g_object_get(G_OBJECT(pixbuf), "width", &width, "height", &height, nullptr);
  cursors[CURSOR_ZOOM_IN] =
    gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
  g_object_unref(G_OBJECT(pixbuf));

  pixbuf = gtk_widget_render_icon_pixbuf(w, GTK_STOCK_ZOOM_OUT, size);
  g_object_get(G_OBJECT(pixbuf), "width", &width, "height", &height, nullptr);
  cursors[CURSOR_ZOOM_OUT] =
    gdk_cursor_new_from_pixbuf(display, pixbuf, width / 2, height / 2);
  g_object_unref(G_OBJECT(pixbuf));

  cursors[CURSOR_IBEAM] = gdk_cursor_new(GDK_XTERM);
  cursors[CURSOR_HREF] = gdk_cursor_new(GDK_HAND2);
}

void set_cursor(GdkWindow *window, GtkWidget *w, int mode)
{
  init_cursors(w);
  GdkCursor *cursor_new = cursors[mode];
  if (cursor_new != cursor_current)
  {
    gdk_window_set_cursor(window, cursor_current = cursor_new);
    g_object_unref(cursor_current);
  }
}

void set_cursor_from_iter(GdkWindow *window, GtkWidget *w, GtkTextIter *it)
{
  GtkTextView *text_view(GTK_TEXT_VIEW(w));
  GtkTextBuffer *buf(gtk_text_view_get_buffer(text_view));
  GtkTextTagTable *tags(gtk_text_buffer_get_tag_table(buf));
  GtkTextTag *pix_tag(gtk_text_tag_table_lookup(tags, "pixbuf"));
  GtkTextTag *url_tag(gtk_text_tag_table_lookup(tags, "url"));
  bool const in_url(gtk_text_iter_has_tag(it, url_tag));
  bool const in_pix(gtk_text_iter_has_tag(it, pix_tag));
  bool const fullsize(get_fullsize_flag(buf));

  int mode;
  if (in_pix && fullsize)
  {
    mode = CURSOR_ZOOM_OUT;
  }
  else if (in_pix)
  {
    mode = CURSOR_ZOOM_IN;
  }
  else if (in_url)
  {
    mode = CURSOR_HREF;
  }
  else
  {
    mode = CURSOR_IBEAM;
  }
  set_cursor(window, w, mode);
}
} // namespace

namespace {
GtkTextTag *get_named_tag_from_view(GtkWidget *w, char const *key)
{
  GtkTextView *text_view(GTK_TEXT_VIEW(w));
  GtkTextBuffer *buf = gtk_text_view_get_buffer(text_view);
  GtkTextTagTable *tags = gtk_text_buffer_get_tag_table(buf);
  return gtk_text_tag_table_lookup(tags, key);
}

void get_iter_from_event_coords(GtkWidget *w, int x, int y, GtkTextIter *setme)
{
  GtkTextView *text_view(GTK_TEXT_VIEW(w));
  gtk_text_view_window_to_buffer_coords(
    text_view, GTK_TEXT_WINDOW_WIDGET, x, y, &x, &y);
  gtk_text_view_get_iter_at_location(text_view, setme, x, y);
}

std::string get_url_from_iter(GtkWidget *w, GtkTextIter *iter)
{
  std::string ret;
  GtkTextTag *url_tag(get_named_tag_from_view(w, "url"));
  if (gtk_text_iter_has_tag(iter, url_tag))
  {
    GtkTextIter begin(*iter), end(*iter);
    if (! gtk_text_iter_begins_tag(&begin, url_tag))
    {
      gtk_text_iter_backward_to_tag_toggle(&begin, nullptr);
    }
    gtk_text_iter_forward_to_tag_toggle(&end, nullptr);
    char *pch = gtk_text_iter_get_text(&begin, &end);
    if (pch)
    {
      ret = pch;
      g_free(pch);
    }
  }
  return ret;
}

gboolean motion_notify_event(GtkWidget *w,
                             GdkEventMotion *event,
                             gpointer hover_url)
{
  if (event->window != nullptr)
  {
    int x, y;
    if (event->is_hint)
    {
      gdk_window_get_device_position(
        event->window, event->device, &x, &y, nullptr);
    }
    else
    {
      x = (int)event->x;
      y = (int)event->y;
    }
    GtkTextIter iter;
    get_iter_from_event_coords(w, x, y, &iter);
    set_cursor_from_iter(event->window, w, &iter);
    *static_cast<std::string *>(hover_url) = get_url_from_iter(w, &iter);
  }

  return false;
}

/* returns a GdkPixbuf of the scaled image.
   unref it when no longer needed. */
GdkPixbuf *size_to_fit(GdkPixbuf *pixbuf, GtkAllocation const *size)
{
  int const nw(size ? size->width : 0);
  int const nh(size ? size->height : 0);

  GdkPixbuf *out(nullptr);
  if (nw >= 100 && nh >= 100)
  {
    int const ow(gdk_pixbuf_get_width(pixbuf));
    int const oh(gdk_pixbuf_get_height(pixbuf));
    double scale_factor(std::min(nw / (double)ow, nh / (double)oh));
    scale_factor = std::min(scale_factor, 1.0);
    int const scaled_width((int)std::floor(ow * scale_factor + 0.5));
    int const scaled_height((int)std::floor(oh * scale_factor + 0.5));
    out = gdk_pixbuf_scale_simple(
      pixbuf, scaled_width, scaled_height, GDK_INTERP_BILINEAR);
  }

  if (! out)
  {
    g_object_ref(pixbuf);
    out = pixbuf;
  }

  return out;
}

void resize_picture_at_iter(GtkTextBuffer *buf,
                            GtkTextIter *iter,
                            bool fullsize,
                            GtkAllocation const *size,
                            GtkTextTag *apply_tag)
{

  int const begin_offset(gtk_text_iter_get_offset(iter));

  GdkPixbuf *original(nullptr);
  GdkPixbuf *old_scaled(nullptr);
  if (! get_pixbuf_at_offset(buf, begin_offset, original, old_scaled))
  {
    return;
  }

  GdkPixbuf *new_scaled(size_to_fit(original, (fullsize ? nullptr : size)));
  int const old_w(gdk_pixbuf_get_width(old_scaled));
  int const new_w(gdk_pixbuf_get_width(new_scaled));
  int const old_h(gdk_pixbuf_get_height(old_scaled));
  int const new_h(gdk_pixbuf_get_height(new_scaled));
  if (old_w != new_w || old_h != new_h)
  {
    // remove the old..
    GtkTextIter old_end(*iter);
    gtk_text_iter_forward_to_tag_toggle(&old_end, apply_tag);
    gtk_text_buffer_delete(buf, iter, &old_end);

    // insert the new..
    gtk_text_buffer_insert_pixbuf(buf, iter, new_scaled);
    gtk_text_buffer_insert(buf, iter, "\n", -1);
    set_pixbuf_at_offset(buf, begin_offset, original, new_scaled);

    // and apply the tag.
    GtkTextIter begin(*iter);
    gtk_text_iter_set_offset(&begin, begin_offset);
    gtk_text_buffer_apply_tag(buf, apply_tag, &begin, iter);
  }

  g_object_unref(new_scaled);
}
} // namespace

gboolean BodyPane ::mouse_button_pressed_cb(GtkWidget *w,
                                            GdkEventButton *e,
                                            gpointer p)
{
  return static_cast<BodyPane *>(p)->mouse_button_pressed(w, e);
}

gboolean BodyPane ::mouse_button_pressed(GtkWidget *w, GdkEventButton *event)
{
  g_return_val_if_fail(GTK_IS_TEXT_VIEW(w), false);

  if (event->button == 1 || event->button == 2)
  {
    std::string const &url(_hover_url);
    if (! url.empty())
    {
      /* this is a crude way of making sure that double-click
       * doesn't open two or three browser windows. */
      static time_t last_url_time(0);
      time_t const this_url_time(time(nullptr));
      if (this_url_time != last_url_time)
      {
        last_url_time = this_url_time;
        URL ::open(_prefs, url.c_str());
      }
    }
    else
    { // maybe we're zooming in/out on a pic...
      GtkTextIter iter;
      GtkTextTag *pix_tag(get_named_tag_from_view(w, "pixbuf"));
      get_iter_from_event_coords(w, (int)event->x, (int)event->y, &iter);
      if (gtk_text_iter_has_tag(&iter, pix_tag))
      {
        if (! gtk_text_iter_starts_tag(&iter, pix_tag))
        {
          gtk_text_iter_backward_to_tag_toggle(&iter, pix_tag);
        }
        g_assert(gtk_text_iter_starts_tag(&iter, pix_tag));

        // percent_x,percent_y reflect where the user clicked in the picture
        GdkRectangle rec;
        gtk_text_view_get_iter_location(GTK_TEXT_VIEW(w), &iter, &rec);
        int buf_x, buf_y;
        gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(w),
                                              GTK_TEXT_WINDOW_WIDGET,
                                              (gint)event->x,
                                              (gint)event->y,
                                              &buf_x,
                                              &buf_y);
        double const percent_x = (buf_x - rec.x) / (double)rec.width;
        double const percent_y = (buf_y - rec.y) / (double)rec.height;

        // resize the picture and refresh `iter'
        int const offset(gtk_text_iter_get_offset(&iter));
        GtkTextBuffer *buf(gtk_text_view_get_buffer(GTK_TEXT_VIEW(w)));
        bool const fullsize(toggle_fullsize_flag(buf));
        GtkAllocation aloc;
        gtk_widget_get_allocation(w, &aloc);
        //        std::cerr<<"457 alloc "<<aloc.x<<" "<<aloc.y<<"
        //        "<<aloc.width<<" "<<aloc.height<<"\n";
        resize_picture_at_iter(buf, &iter, fullsize, &aloc, pix_tag);
        gtk_text_iter_set_offset(&iter, offset);
        set_cursor_from_iter(event->window, w, &iter);

        // x2,y2 are to position percent_x,percent_y in the middle of the
        // window.
        GtkTextMark *mark =
          gtk_text_buffer_create_mark(buf, nullptr, &iter, true);
        double const x2 = CLAMP((percent_x + (percent_x - 0.5)), 0.0, 1.0);
        double const y2 = CLAMP((percent_y + (percent_y - 0.5)), 0.0, 1.0);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(w), mark, 0.0, true, x2, y2);
        gtk_text_buffer_delete_mark(buf, mark);
      }
    }
  }
  return false;
}

/***
****  INIT
***/
namespace {

GtkTextTag *get_or_create_tag(GtkTextTagTable *table, char const *key)
{
  g_assert(table);
  g_assert(key && *key);

  GtkTextTag *tag(gtk_text_tag_table_lookup(table, key));
  if (! tag)
  {
    tag = gtk_text_tag_new(key);
    gtk_text_tag_table_add(table, tag);
    g_object_unref(tag); // table refs it
  }
  return tag;
}

void set_text_buffer_tags(GtkTextBuffer *buffer, Prefs const &p)
{
  PanColors const &colors(PanColors::get());
  std::string const fg(p.get_color_str("text-color-fg", colors.def_fg));
  std::string const bg(p.get_color_str("text-color-bg", colors.def_bg));

  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);

  get_or_create_tag(table, "pixbuf");
  get_or_create_tag(table, "quote_0");

  g_object_set(get_or_create_tag(table, "text"),
               "foreground",
               fg.c_str(),
               "background",
               bg.c_str(),
               nullptr);
  g_object_set(
    get_or_create_tag(table, "bold"), "weight", PANGO_WEIGHT_BOLD, nullptr);
  g_object_set(
    get_or_create_tag(table, "italic"), "style", PANGO_STYLE_ITALIC, nullptr);
  g_object_set(get_or_create_tag(table, "underline"),
               "underline",
               PANGO_UNDERLINE_SINGLE,
               nullptr);
  g_object_set(
    get_or_create_tag(table, "url"),
    "underline",
    PANGO_UNDERLINE_SINGLE,
    "foreground",
    p.get_color_str("body-pane-color-url", TANGO_SKY_BLUE_DARK).c_str(),
    "background",
    p.get_color_str("body-pane-color-url-bg", bg).c_str(),
    nullptr);
  g_object_set(
    get_or_create_tag(table, "quote_1"),
    "foreground",
    p.get_color_str("body-pane-color-quote-1", TANGO_CHAMELEON_DARK).c_str(),
    "background",
    p.get_color_str("body-pane-color-quote-1-bg", bg).c_str(),
    nullptr);
  g_object_set(
    get_or_create_tag(table, "quote_2"),
    "foreground",
    p.get_color_str("body-pane-color-quote-2", TANGO_ORANGE_DARK).c_str(),
    "background",
    p.get_color_str("body-pane-color-quote-2-bg", bg).c_str(),
    nullptr);
  g_object_set(
    get_or_create_tag(table, "quote_3"),
    "foreground",
    p.get_color_str("body-pane-color-quote-3", TANGO_PLUM_DARK).c_str(),
    "background",
    p.get_color_str("body-pane-color-quote-3-bg", bg).c_str(),
    nullptr);
  g_object_set(
    get_or_create_tag(table, "signature"),
    "foreground",
    p.get_color_str("body-pane-color-signature", TANGO_SKY_BLUE_LIGHT).c_str(),
    "background",
    p.get_color_str("body-pane-color-signature-bg", bg).c_str(),
    nullptr);
}
} // namespace

/***
****
***/
namespace {
// handle up, down, pgup, pgdown to scroll
gboolean text_key_pressed(GtkWidget *w, GdkEventKey *event, gpointer scroll)
{
  gboolean handled(false);

  g_return_val_if_fail(GTK_IS_TEXT_VIEW(w), false);

  bool const up = event->keyval == GDK_KEY_Up || event->keyval == GDK_KEY_KP_Up;
  bool const down =
    event->keyval == GDK_KEY_Down || event->keyval == GDK_KEY_KP_Down;

  if (up || down)
  {
    handled = true;
    gtk_text_view_place_cursor_onscreen(GTK_TEXT_VIEW(w));
    GtkAdjustment *adj =
      gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));
    gdouble val = gtk_adjustment_get_value(adj);
    if (up)
    {
      val -= gtk_adjustment_get_step_increment(adj);
    }
    else
    {
      val += gtk_adjustment_get_step_increment(adj);
    }
    val = MAX(val, gtk_adjustment_get_lower(adj));
    val = MIN(
      val, gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj));
    gtk_adjustment_set_value(adj, val);
  }

  return handled;
}
} // namespace

/****
*****
*****   SETTING THE TEXT FROM AN ARTICLE
*****
****/
namespace {
bool text_get_show_all_headers()
{
  return false;
}

/**
 * Returns the quote tag ("quote_0", "quote_1", etc) appropriate for the line.
 * The right tag is calculated by adding up the number of quote characters
 * at the beginning of the line.
 *
 * @param utf8_line the line whose quote status we're checking
 * @param utf8_byte_len the byte length of utf8_line
 * @return a const string for the line's quote tag.  Never nullptr.
 */
char const *get_quote_tag(TextMassager const *text_massager,
                          char const *utf8_line,
                          int utf8_byte_len)
{
  char const *str = utf8_line;
  char const *retval = "quote_0";

  if (0 < utf8_byte_len && str && *str)
  {
    char const *line_end = utf8_line + utf8_byte_len;
    int depth = 0;

    // walk past leading spaces
    while (str != line_end && g_unichar_isspace(g_utf8_get_char(str)))
    {
      str = g_utf8_next_char(str);
    }

    // count the number of spaces or quote characters
    for (;;)
    {
      if (str == line_end)
      {
        break;
      }
      else if (text_massager->is_quote_character(g_utf8_get_char(str)))
      {
        ++depth;
      }
      else if (! g_unichar_isspace(g_utf8_get_char(str)))
      {
        break;
      }
      str = g_utf8_next_char(str);
    }

    if (depth != 0)
    {
      switch (depth % 3)
      {
        case 1:
          retval = "quote_1";
          break;
        case 2:
          retval = "quote_2";
          break;
        case 0:
          retval = "quote_3";
          break;
      }
    }
  }

  return retval;
}

typedef std::map<std::string, GdkPixbuf *> pixbufs_t;

// don't use this directly -- use get_emoticons()
pixbufs_t emoticon_pixbufs;

void free_emoticons()
{
  foreach_const (pixbufs_t, emoticon_pixbufs, it)
  {
    g_object_unref(it->second);
  }
}

pixbufs_t &get_emoticons()
{
  return emoticon_pixbufs;
}

void create_emoticons()
{
  emoticon_pixbufs[":)"] = load_icon("icon_mozilla_smile.png");
  emoticon_pixbufs[":-)"] = load_icon("icon_mozilla_smile.png");
  emoticon_pixbufs[";)"] = load_icon("icon_mozilla_wink.png");
  emoticon_pixbufs[":("] = load_icon("icon_mozilla_frown.png");
  emoticon_pixbufs[":P"] = load_icon("icon_mozilla_tongueout.png");
  emoticon_pixbufs[":O"] = load_icon("icon_mozilla_surprised.png");
}

enum TagMode
{
  ADD,
  REPLACE
};

void set_section_tag(GtkTextBuffer *buffer,
                     GtkTextIter *start,
                     StringView const &body,
                     StringView const &area,
                     char const *tag,
                     TagMode mode)
{
  // if no alnums, chances are area is a false positive
  int alnums(0);
  for (char const *pch(area.begin()), *end(area.end()); pch != end; ++pch)
  {
    if (::isalnum(*pch))
    {
      ++alnums;
    }
  }
  if (! alnums)
  {
    return;
  }

  GtkTextIter mark_start = *start;
  gtk_text_iter_forward_chars(&mark_start,
                              g_utf8_strlen(body.str, area.str - body.str));
  GtkTextIter mark_end = mark_start;
  gtk_text_iter_forward_chars(&mark_end, g_utf8_strlen(area.str, area.len));
  if (mode == REPLACE)
  {
    gtk_text_buffer_remove_all_tags(buffer, &mark_start, &mark_end);
  }
  gtk_text_buffer_apply_tag_by_name(buffer, tag, &mark_start, &mark_end);
}

void show_signature(GtkTextBuffer *buffer,
                    GtkTextMark *mark,
                    std::string &body,
                    bool show)
{
  GtkTextTagTable *tags(gtk_text_buffer_get_tag_table(buffer));
  if (tags)
  {
    GtkTextTag *sig_tag(gtk_text_tag_table_lookup(tags, "signature"));
    if (sig_tag)
    {
      g_object_set(sig_tag, "invisible", ! show, nullptr);
    }
  }
}

void replace_emoticon_text_with_pixbuf(GtkTextBuffer *buffer,
                                       GtkTextMark *mark,
                                       std::string &body,
                                       std::string const &text,
                                       GdkPixbuf *pixbuf)
{
  g_assert(! text.empty());
  if (pixbuf == nullptr)
    {
      // emoticon icon was not found. No need to crash pan
      std::cout << "No icon loaded for emoticon " << text << std::endl;
      return;
    }

  GtkTextTagTable *tags(gtk_text_buffer_get_tag_table(buffer));
  GtkTextTag *url_tag(gtk_text_tag_table_lookup(tags, "url"));

  size_t const n(text.size());
  std::string::size_type pos(0);
  while (((pos = body.find(text, pos))) != body.npos)
  {
    GtkTextIter begin;
    gtk_text_buffer_get_iter_at_mark(buffer, &begin, mark);
    gtk_text_iter_forward_chars(&begin, g_utf8_strlen(&body[0], pos));

    if (gtk_text_iter_has_tag(&begin, url_tag))
    {
      pos += n;
    }
    else
    {
      GtkTextIter end(begin);
      gtk_text_iter_forward_chars(&end, n);
      gtk_text_buffer_delete(buffer, &begin, &end);
      body.erase(pos, text.size());
      gtk_text_buffer_insert_pixbuf(buffer, &end, pixbuf);
      body.insert(pos, 1, '?'); // make body.size() match the textbuf's size
    }
  }
}

/**
 * Appends the specified body into the text buffer.
 * This function takes care of muting quotes and marking
 * quoted and URL areas in the GtkTextBuffer.
 */
void append_text_buffer_nolock(TextMassager const *text_massager,
                               GtkTextBuffer *buffer,
                               StringView const &body_in,
                               bool mute_quotes,
                               bool show_smilies,
                               bool show_sig,
                               bool do_markup,
                               bool do_urls)
{

  g_return_if_fail(buffer != nullptr);
  g_return_if_fail(GTK_IS_TEXT_BUFFER(buffer));

  // mute the quoted text, if desired
  std::string body;
  if (mute_quotes)
  {
    body = text_massager->mute_quotes(body_in);
  }
  else
  {
    body.assign(body_in.str, body_in.len);
  }

  // insert the text
  GtkTextIter end;
  gtk_text_buffer_get_end_iter(buffer, &end);
  GtkTextMark *mark = gtk_text_buffer_create_mark(buffer, "blah", &end, true);
  gtk_text_buffer_insert(buffer, &end, body.c_str(), -1);
  GtkTextIter start;
  gtk_text_buffer_get_iter_at_mark(buffer, &start, mark);

  StringView v(body), line;
  GtkTextIter mark_start(start);

  // colorize text
  gtk_text_buffer_apply_tag_by_name(buffer, "text", &mark_start, &end);

  // find where the signature begins...
  char const *sig_point(nullptr);
  int offset(0);
  if (GNKSA::find_signature_delimiter(v, offset) != GNKSA::SIG_NONE)
  {
    sig_point = v.str + offset;
  }

  // colorize the quoted text
  GtkTextIter mark_end;
  std::string last_quote_tag;
  bool is_sig(false);
  char const *last_quote_begin(v.str);
  while (v.pop_token(line, '\n'))
  {
    if (line.empty())
    {
      continue;
    }

    if (line.str == sig_point)
    {
      is_sig = true;
    }

    std::string const quote_tag =
      is_sig ? "signature" : get_quote_tag(text_massager, line.str, line.len);

    // if we've changed tags, colorize the previous block
    if (! last_quote_tag.empty() && quote_tag != last_quote_tag)
    {
      mark_end = mark_start;
      gtk_text_iter_forward_chars(
        &mark_end,
        g_utf8_strlen(last_quote_begin, line.str - 1 - last_quote_begin));
      gtk_text_buffer_apply_tag_by_name(
        buffer, last_quote_tag.c_str(), &mark_start, &mark_end);
      mark_start = mark_end;
      gtk_text_iter_forward_chars(&mark_start, 1);
      last_quote_begin = line.str;
    }

    last_quote_tag = quote_tag;
  }

  // apply the final tag, if there is one */
  if (! last_quote_tag.empty())
  {
    gtk_text_buffer_get_end_iter(buffer, &mark_end);
    gtk_text_buffer_apply_tag_by_name(
      buffer, last_quote_tag.c_str(), &mark_start, &mark_end);
  }

  // apply markup
  StringView const v_all(body);
  if (do_markup)
  {
    bool is_patch = false;
    std::set<char const *> already_processed;
    StringView v(body), line;
    while (v.pop_token(line, '\n'))
    {
      // if we detect that this message contains a patch,
      // turn off markup for the rest of the article.
      if (! is_patch)
      {
        is_patch =
          (line.strstr("--- ") == line.str) || (line.strstr("@@ ") == line.str);
      }
      if (is_patch)
      {
        continue;
      }

      for (;;)
      {
        // find the first markup character.
        // if we've already used it,
        // or if it was preceeded by something not a space or punctuation,
        // then keep looking.
        char const *b(line.strpbrk("_*/"));
        if (! b)
        {
          break;
        }
        if (already_processed.count(b)
            || (b != &line.front() && ! isspace(b[-1]) && ! ispunct(b[-1])))
        {
          line.eat_chars(b + 1 - line.str);
          continue;
        }

        // find the ending corresponding markup character.
        // if it was followed by something not a space or punctuation, keep
        // looking.
        char const *e = b;
        while ((e = line.strchr(*b, 1 + (e - line.str))))
        {
          if (e == &line.back() || isspace(e[1]) || ispunct(e[1])
              || strchr("_*/", e[1]))
          {
            break;
          }
        }

        if (e)
        {
          already_processed.insert(e);
          char const *type(nullptr);
          switch (*b)
          {
            case '*':
              type = "bold";
              break;
            case '_':
              type = "underline";
              break;
            case '/':
              type = "italic";
              break;
          }
          set_section_tag(
            buffer, &start, v_all, StringView(b, e + 1), type, ADD);
        }

        line.eat_chars(b + 1 - line.str);
      }
    }
  }

  // colorize urls
  if (do_urls)
  {
    StringView area;
    StringView march(v_all);
    while ((url_find(march, area)))
    {
      // Do no highlight URL in signature when not showing signature
      if (area.str > sig_point && ! show_sig)
      {
        break;
      }
      set_section_tag(buffer, &start, v_all, area, "url", REPLACE);
      march = march.substr(area.str + area.len, nullptr);
    }
  }

  // do this last, since it alters the text instead of just marking it up
  if (show_smilies)
  {
    pixbufs_t &emoticons(get_emoticons());
    foreach_const (pixbufs_t, emoticons, it)
    {
      replace_emoticon_text_with_pixbuf(
        buffer, mark, body, it->first, it->second);
    }
  }

  show_signature(buffer, mark, body, show_sig);

  gtk_text_buffer_delete_mark(buffer, mark);
}

/**
 * Generates a GtkPixmap object from a given GMimePart that contains an image.
 * Used for displaying attached pictures inline.
 */
GdkPixbuf *get_pixbuf_from_gmime_part(GMimePart *part)
{
  GdkPixbufLoader *l(gdk_pixbuf_loader_new());
  GError *err(nullptr);

  // populate the loader
  GMimeDataWrapper *wrapper(g_mime_part_get_content(part));
  if (wrapper)
  {
    GMimeStream *mem_stream(g_mime_stream_mem_new());
    g_mime_data_wrapper_write_to_stream(wrapper, mem_stream);
    GByteArray *buffer(GMIME_STREAM_MEM(mem_stream)->buffer);
    gsize bytesLeft = buffer->len;
    guchar *data = buffer->data;

    // ticket #467446 - workaround gdkpixbuf <= 2.12.x's
    // jpg loader bug (#494667) by feeding the loader in
    // smaller chunks
    while (bytesLeft > 0)
    {
      gsize const n = MIN(4096, bytesLeft);

      gdk_pixbuf_loader_write(l, data, n, &err);
      if (err)
      {
        Log::add_err(err->message);
        g_clear_error(&err);
        break;
      }

      bytesLeft -= n;
      data += n;
    }

    g_object_unref(mem_stream);
  }

  // flush the loader
  gdk_pixbuf_loader_close(l, &err);
  if (err)
  {
    Log::add_err(err->message);
    g_clear_error(&err);
  }

  // create the pixbuf
  GdkPixbuf *pixbuf(nullptr);
  if (! err)
  {
    pixbuf = gdk_pixbuf_loader_get_pixbuf(l);
  }
  else
  {
    Log::add_err(err->message);
    g_clear_error(&err);
  }

  // cleanup
  if (pixbuf)
  {
    g_object_ref(G_OBJECT(pixbuf));
  }
  g_object_unref(G_OBJECT(l));
  return pixbuf;
}

} // namespace

void BodyPane ::append_part(GMimeObject *parent,
                            GMimeObject *obj,
                            GtkAllocation *widget_size)
{
  bool is_done(false);

  // we only need leaf parts..
  if (! GMIME_IS_PART(obj))
  {
    return;
  }

  GMimePart *part = GMIME_PART(obj);
  GMimeContentType *type = g_mime_object_get_content_type(GMIME_OBJECT(part));

  // decide whether or not this part is a picture
  bool is_image(g_mime_content_type_is_type(type, "image", "*"));
  if (! is_image
      && g_mime_content_type_is_type(type, "application", "octet-stream"))
  {
    char const *type, *subtype;
    mime::guess_part_type_from_filename(
      g_mime_part_get_filename(part), &type, &subtype);
    is_image = type && ! strcmp(type, "image");
  }

  // if it's a picture, draw it
  if (is_image)
  {
    GdkPixbuf *original(get_pixbuf_from_gmime_part(part));
    bool const fullsize(! _prefs.get_flag("size-pictures-to-fit", true));
    GdkPixbuf *scaled(size_to_fit(original, fullsize ? nullptr : widget_size));

    if (scaled != nullptr)
    {
      GtkTextIter iter;

      // if this is the first thing in the buffer, precede it with a linefeed.
      gtk_text_buffer_get_end_iter(_buffer, &iter);
      if (gtk_text_buffer_get_char_count(_buffer) > 0)
      {
        gtk_text_buffer_insert(_buffer, &iter, "\n", -1);
      }

      // rembember the location of the first picture.
      if (gtk_text_buffer_get_mark(_buffer, FIRST_PICTURE) == nullptr)
      {
        gtk_text_buffer_create_mark(_buffer, FIRST_PICTURE, &iter, true);
      }
      gtk_text_buffer_create_mark(_buffer, FIRST_PICTURE, &iter, true);

      // add the picture
      int const begin_offset(gtk_text_iter_get_offset(&iter));
      gtk_text_buffer_insert_pixbuf(_buffer, &iter, scaled);
      gtk_text_buffer_insert(_buffer, &iter, "\n", -1);
      GtkTextIter iter_begin(iter);
      gtk_text_iter_set_offset(&iter_begin, begin_offset);

      // hook onto the tag a reference to the original picture
      // so that we can resize it later if user resizes the text pane.
      set_pixbuf_at_offset(_buffer, begin_offset, original, scaled);
      set_fullsize_flag(_buffer, fullsize);

      GtkTextTagTable *tags(gtk_text_buffer_get_tag_table(_buffer));
      GtkTextTag *tag(gtk_text_tag_table_lookup(tags, "pixbuf"));
      gtk_text_buffer_apply_tag(_buffer, tag, &iter_begin, &iter);

      g_object_unref(scaled);
      g_object_unref(original);

      is_done = true;
    }
  }

  // or, if it's text, display it
  else if (g_mime_content_type_is_type(type, "text", "*"))
  {
    char const *fallback_charset(_charset.c_str());
    char const *p_flowed(
      g_mime_object_get_content_type_parameter(obj, "format"));
    bool const flowed(g_strcmp0(p_flowed, "flowed") == 0);
    std::string str = mime_part_to_utf8(part, fallback_charset);

    if (! str.empty() && _prefs.get_flag("wrap-article-body", false))
    {
      str = _tm.fill(str, flowed);
    }

    bool const do_mute(_prefs.get_flag("mute-quoted-text", false));
    bool const do_smilies(_prefs.get_flag("show-smilies-as-graphics", true));
    bool const do_markup(_prefs.get_flag("show-text-markup", true));
    bool const do_urls(_prefs.get_flag("highlight-urls", true));
    bool const do_sig(_prefs.get_flag("show-article-sig", true));
    append_text_buffer_nolock(
      &_tm, _buffer, str, do_mute, do_smilies, do_sig, do_markup, do_urls);
    is_done = true;
  }

  // picture or otherwise, add to list of attachments
  if (is_image || ! is_done)
  {
    char const *filename = g_mime_part_get_filename(part);
    char *pch = (filename && *filename) ? g_strdup_printf("%s", filename) :
                                          g_strdup_printf(_("Unnamed File"));

    add_attachment_to_toolbar(pch);

    _attach_names.insert(pch);
  }
}

void BodyPane ::foreach_part_cb(GMimeObject *parent,
                                GMimeObject *o,
                                gpointer self)
{
  BodyPane *pane = static_cast<BodyPane *>(self);
  GtkWidget *w(pane->_text);
  GtkAllocation aloc;
  gtk_widget_get_allocation(w, &aloc);
  pane->append_part(parent, o, &aloc);
}

/***
****  HEADERS
***/
namespace {
void add_bold_header_value(std::string &s,
                           GMimeMessage *message,
                           char const *key,
                           char const *fallback_charset)
{
  char const *val(
    message ? g_mime_object_get_header((GMimeObject *)message, key) : "");
  std::string const utf8_val(header_to_utf8(val, fallback_charset));
  char *e(nullptr);
  if (strcmp(key, "From"))
  {
    e = g_markup_printf_escaped("<span weight=\"bold\">%s</span>",
                                utf8_val.c_str());
  }
  else
  {
    StringView const v = GNKSA ::get_short_author_name(utf8_val);
    e = g_markup_printf_escaped("<span weight=\"bold\">%s</span>",
                                v.to_string().c_str());
  }
  s += e;
  g_free(e);
}

size_t add_header_line(std::string &s,
                       char const *key_i18n,
                       char const *key,
                       char const *val,
                       char const *fallback_charset)
{
  char *e;
  e = g_markup_printf_escaped("<span weight=\"bold\">%s:</span> ", key_i18n);
  s += e;
  g_free(e);
  std::string const utf8_val(header_to_utf8(val, fallback_charset));
  e = g_markup_printf_escaped("%s\n", utf8_val.c_str());
  s += e;
  size_t const retval(g_utf8_strlen(key, -1)
                      + g_utf8_strlen(utf8_val.c_str(), -1) + 2);
  g_free(e);

  return retval;
}

size_t add_header_line(std::string &s,
                       GMimeMessage *msg,
                       char const *key_i18n,
                       char const *key,
                       char const *fallback_charset)
{
  char const *val(msg ? g_mime_object_get_header((GMimeObject *)msg, key) : "");
  return add_header_line(s, key_i18n, key, val, fallback_charset);
}

} // namespace

void BodyPane ::set_text_from_message(GMimeMessage *message)
{
  char const *fallback_charset(_charset.empty() ? nullptr : _charset.c_str());

  // mandatory headers...
  std::string s;
  size_t w(0), l(0);
  l = add_header_line(s, message, _("Subject"), "Subject", fallback_charset);
  w = std::max(w, l);
  l = add_header_line(s, message, _("From"), "From", fallback_charset);
  w = std::max(w, l);
  l = add_header_line(s, message, _("Date"), "Date", fallback_charset);
  w = std::max(w, l);

  // conditional headers...
  if (message)
  {
    StringView const newsgroups(
      g_mime_object_get_header((GMimeObject *)message, "Newsgroups"));
    if (newsgroups.strchr(','))
    {
      l = add_header_line(
        s, message, _("Newsgroups"), "Newsgroups", fallback_charset);
      w = std::max(w, l);
    }
    StringView const user_agent(
      g_mime_object_get_header((GMimeObject *)message, "User-Agent"));
    if (! user_agent.empty())
    {
      l = add_header_line(
        s, message, _("User-Agent"), "User-Agent", fallback_charset);
      w = std::max(w, l);
    }
    StringView const x_newsreader(
      g_mime_object_get_header((GMimeObject *)message, "X-Newsreader"));
    if (! x_newsreader.empty())
    {
      l = add_header_line(
        s, message, _("User-Agent"), "X-Newsreader", fallback_charset);
      w = std::max(w, l);
    }
    StringView const x_mailer(
      g_mime_object_get_header((GMimeObject *)message, "X-Mailer"));
    if (! x_mailer.empty())
    {
      l = add_header_line(
        s, message, _("User-Agent"), "X-Mailer", fallback_charset);
      w = std::max(w, l);
    }
    StringView const followup_to(
      g_mime_object_get_header((GMimeObject *)message, "Followup-To"));
    if (! followup_to.empty() && (followup_to != newsgroups))
    {
      l = add_header_line(
        s, message, _("Followup-To"), "Followup-To", fallback_charset);
      w = std::max(w, l);
    }
    StringView const reply_to(
      g_mime_object_get_header((GMimeObject *)message, "Reply-To"));
    if (! reply_to.empty())
    {
      StringView const from(
        g_mime_object_get_header((GMimeObject *)message, "From"));
      StringView f_addr, f_name, rt_addr, rt_name;
      GNKSA ::do_check_from(from, f_addr, f_name, false);
      GNKSA ::do_check_from(reply_to, rt_addr, rt_name, false);
      if (f_addr != rt_addr)
      {
        l = add_header_line(
          s, message, _("Reply-To"), "Reply-To", fallback_charset);
        w = std::max(w, l);
      }
    }
  }

  s.resize(std::max((size_t)0, s.size() - 1)); // remove trailing linefeed
  gtk_label_set_markup(GTK_LABEL(_headers), s.c_str());

  // set the x-face...
  gtk_image_clear(GTK_IMAGE(_xface));
  char const *pch =
    message ? g_mime_object_get_header((GMimeObject *)message, "X-Face") :
              nullptr;
  if (pch && gtk_widget_get_window(_xface))
  {
    gtk_widget_set_size_request(_xface, FACE_SIZE, FACE_SIZE);
    GdkPixbuf *pixbuf = nullptr;
    pixbuf = pan_gdk_pixbuf_create_from_x_face(pch);
    gtk_image_set_from_pixbuf(GTK_IMAGE(_xface), pixbuf);
    g_object_unref(pixbuf);
  }
  else
  {
    gtk_widget_set_size_request(_xface, 0, FACE_SIZE);
  }

  // set the face
  gtk_image_clear(GTK_IMAGE(_face));
  pch = message ? g_mime_object_get_header((GMimeObject *)message, "Face") :
                  nullptr;
  if (pch && gtk_widget_get_window(_face))
  {
    gtk_widget_set_size_request(_face, FACE_SIZE, FACE_SIZE);
    GMimeEncoding dec;
    g_mime_encoding_init_decode(&dec, GMIME_CONTENT_ENCODING_BASE64);
    guchar *buf = new guchar[strlen(pch)];
    int len = g_mime_encoding_step(&dec, pch, strlen(pch), (char *)buf);
    GdkPixbufLoader *pl = gdk_pixbuf_loader_new_with_type("png", nullptr);
    gdk_pixbuf_loader_write(pl, buf, len, nullptr);
    gdk_pixbuf_loader_close(pl, nullptr);
    GdkPixbuf *pixbuf = gdk_pixbuf_loader_get_pixbuf(pl);
    gtk_image_set_from_pixbuf(GTK_IMAGE(_face), pixbuf);
    g_object_unref(pl);
    delete[] buf;
  }
  else
  {
    gtk_widget_set_size_request(_face, 0, FACE_SIZE);
  }

  // set the terse headers...
  s.clear();
  add_bold_header_value(s, message, "Subject", fallback_charset);
  s += _(" from ");
  add_bold_header_value(s, message, "From", fallback_charset);
  s += _(" at ");
  add_bold_header_value(s, message, "Date", fallback_charset);
  gtk_label_set_markup(GTK_LABEL(_terse), s.c_str());

  // clear the text buffer...
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(_buffer, &start, &end);
  gtk_text_buffer_delete(_buffer, &start, &end);
  if (gtk_text_buffer_get_mark(_buffer, FIRST_PICTURE) != nullptr)
  {
    gtk_text_buffer_delete_mark_by_name(_buffer, FIRST_PICTURE);
  }
  clear_pixbuf_cache(_buffer);

  // maybe add the headers
  bool const do_show_headers(_prefs.get_flag("show-all-headers", false));
  if (message && do_show_headers)
  {
    char *headers(g_mime_object_get_headers((GMimeObject *)message, nullptr));
    GtkTextIter end;
    gtk_text_buffer_get_end_iter(_buffer, &end);
    StringView line, v(headers);
    while (v.pop_token(line, '\n'))
    {
      std::string const h(header_to_utf8(line, fallback_charset));
      gtk_text_buffer_insert(_buffer, &end, h.c_str(), h.size());
      gtk_text_buffer_insert(_buffer, &end, "\n", 1);
    }
    gtk_text_buffer_insert(_buffer, &end, "\n", 1);
    g_free(headers);
  }

  clear_attachments();

  // FIXME: need to set a mark here so that when user hits follow-up,
  // the all-headers don't get included in the followup

  // set the text buffer...
  if (message)
  {
    g_mime_message_foreach(message, foreach_part_cb, this);
  }
  // set the html view
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(_text));
  GtkTextIter _start, _end;
  gtk_text_buffer_get_bounds(buffer, &_start, &_end);
#ifdef HAVE_WEBKIT
  char *buf(gtk_text_buffer_get_text(buffer, &_start, &_end, false));
  if (buf)
  {
    set_html_text(buf);
  }
#endif

  // if there was a picture, scroll to it.
  // otherwise scroll to the top of the body.
  GtkTextMark *mark = gtk_text_buffer_get_mark(_buffer, FIRST_PICTURE);
  if (mark && _prefs.get_flag("focus-on-image", true))
  {
    gtk_text_view_scroll_to_mark(
      GTK_TEXT_VIEW(_text), mark, 0.0, true, 0.0, 0.0);
  }
  else
  {
    GtkTextIter iter;
    gtk_text_buffer_get_start_iter(_buffer, &iter);
    gtk_text_view_scroll_to_iter(
      GTK_TEXT_VIEW(_text), &iter, 0.0, true, 0.0, 0.0);
  }
}

void BodyPane ::refresh()
{
  set_text_from_message(_message);
}

#ifdef HAVE_GMIME_CRYPTO
gboolean BodyPane ::on_verbose_tooltip_cb(GtkWidget *widget,
                                          gint x,
                                          gint y,
                                          gboolean keyboard_tip,
                                          GtkTooltip *tooltip,
                                          gpointer data)
{
  BodyPane *pane = static_cast<BodyPane *>(data);
  if (! pane)
  {
    return false;
  }

  gtk_tooltip_set_icon_from_stock(
    tooltip, GTK_STOCK_DIALOG_INFO, GTK_ICON_SIZE_DIALOG);

  GPGDecErr &err = pane->_gpgerr;
  GPGSignersInfo &info = err.signers;
  if (err.no_sigs)
  {
    return false;
  }
  if (info.signers.empty())
  {
    return false;
  }

  EvolutionDateMaker ed;

  char buf[2048];
  std::pair<std::string, std::string> name_and_email =
    get_email_address(info.signers[0].name);
  g_snprintf(buf,
             sizeof(buf),
             _("<u>This is a <b>PGP-Signed</b> message.</u>\n\n"
               "<b>Signer:</b> %s ('%s')\n"
               "<b>Valid until:</b> %s\n"
               "<b>Created on:</b> %s"),
             name_and_email.first.c_str(),
             name_and_email.second.c_str(),
             (info.signers[0].never_expires ?
                _("always") :
                ed.get_date_string(info.signers[0].expires)),
             ed.get_date_string(info.signers[0].created));

  gtk_tooltip_set_markup(tooltip, buf);

  return true;
}
#endif

void BodyPane ::set_article(const Article &a)
{
  _article = a;

  if (_message)
  {
    g_object_unref(_message);
  }
#ifdef HAVE_GMIME_CRYPTO
  _gpgerr.clear();
  _message = _cache.get_message(_article.get_part_mids(), _gpgerr);
#else
  _message = _cache.get_message(_article.get_part_mids());
#endif

  refresh();

  set_cleared(false);

  _data.mark_read(_article);
}

void BodyPane ::clear()
{
  if (_message)
  {
    g_object_unref(_message);
  }
  _message = nullptr;

  set_cleared(true);

  refresh();
}

void BodyPane ::select_all()
{
  GtkTextIter start, end;
  gtk_text_buffer_get_bounds(_buffer, &start, &end);
  gtk_text_buffer_select_range(_buffer, &start, &end);
}

void BodyPane ::rot13_selected_text()
{
  GtkTextIter start, end;
  if (gtk_text_buffer_get_selection_bounds(_buffer, &start, &end))
  {
    // replace the range with a rot13'ed copy.
    gchar *pch = gtk_text_buffer_get_text(_buffer, &start, &end, false);
    size_t const len = strlen(pch);
    TextMassager ::rot13_inplace(pch);
    gtk_text_buffer_delete(_buffer, &start, &end);
    gtk_text_buffer_insert(_buffer, &end, pch, len);
    g_free(pch);

    // resync selection.
    // since gtk_text_buffer_insert() invalided start, we rebuild it first.
    start = end;
    gtk_text_iter_backward_chars(&start, len);
    gtk_text_buffer_select_range(_buffer, &start, &end);
  }
}

/***
****
***/

gboolean BodyPane ::expander_activated_idle(gpointer self_gpointer)
{
  BodyPane *self(static_cast<BodyPane *>(self_gpointer));
  GtkExpander *ex(GTK_EXPANDER(self->_expander));
  bool const expanded = gtk_expander_get_expanded(ex);
  gtk_expander_set_label_widget(ex, expanded ? self->_verbose : self->_terse);
  self->_prefs.set_flag("body-pane-headers-expanded", expanded);
  return false;
}

void BodyPane ::expander_activated_cb(GtkExpander *, gpointer self_gpointer)
{
  g_idle_add(expander_activated_idle, self_gpointer);
}

void BodyPane ::verbose_clicked_cb(GtkWidget *w,
                                   GdkEvent *event,
                                   gpointer self_gpointer)
{
  BodyPane *self(static_cast<BodyPane *>(self_gpointer));
  GtkExpander *ex(GTK_EXPANDER(self->_expander));
  gtk_expander_set_expanded(ex, ! gtk_expander_get_expanded(ex));
  g_idle_add(expander_activated_idle, self_gpointer);
}

void BodyPane ::refresh_scroll_visible_state()
{
  GtkScrolledWindow *w(GTK_SCROLLED_WINDOW(_scroll));
  GtkAdjustment *adj = gtk_scrolled_window_get_hadjustment(w);
  _hscroll_visible =
    gtk_adjustment_get_page_size(adj) < gtk_adjustment_get_upper(adj);
  adj = gtk_scrolled_window_get_vadjustment(w);
  _vscroll_visible =
    gtk_adjustment_get_page_size(adj) < gtk_adjustment_get_upper(adj);
}

// show*_cb exists to ensure that _hscroll_visible and _vscroll_visible
// are initialized properly when the body pane becomes shown onscreen.

gboolean BodyPane ::show_idle_cb(gpointer pane)
{
  static_cast<BodyPane *>(pane)->refresh_scroll_visible_state();
  return false;
}

void BodyPane ::show_cb(GtkWidget *w G_GNUC_UNUSED, gpointer pane)
{
  g_idle_add(show_idle_cb, pane);
}

namespace {
guint text_size_allocated_idle_tag(0);
}

gboolean BodyPane ::text_size_allocated_idle_cb(gpointer pane)
{
  static_cast<BodyPane *>(pane)->text_size_allocated_idle();
  text_size_allocated_idle_tag = 0;
  return false;
}

void BodyPane ::text_size_allocated_idle()
{

  // prevent oscillation
  bool const old_h(_hscroll_visible);
  bool const old_v(_vscroll_visible);
  refresh_scroll_visible_state();
  if ((old_h != _hscroll_visible) || (old_v != _vscroll_visible))
  {
    return;
  }

  // get the resize flag...
  GtkTextBuffer *buf(gtk_text_view_get_buffer(GTK_TEXT_VIEW(_text)));
  bool const fullsize(get_fullsize_flag(buf));

  // get the start point...
  GtkTextIter iter;
  gtk_text_buffer_get_start_iter(buf, &iter);

  // walk through the buffer looking for pictures to resize
  GtkTextTag *tag(get_named_tag_from_view(_text, "pixbuf"));
  GtkAllocation aloc;
  for (;;)
  {
    if (gtk_text_iter_begins_tag(&iter, tag))
    {
      gtk_widget_get_allocation(_text, &aloc);
      //      std::cerr<<"1449 alloc "<<aloc.x<<" "<<aloc.y<<" "<<aloc.width<<"
      //      "<<aloc.height<<"\n";
      resize_picture_at_iter(buf, &iter, fullsize, &aloc, tag);
    }
    if (! gtk_text_iter_forward_char(&iter))
    {
      break;
    }
    if (! gtk_text_iter_forward_to_tag_toggle(&iter, tag))
    {
      break;
    }
  }
}

void BodyPane ::text_size_allocated(GtkWidget *text G_GNUC_UNUSED,
                                    GtkAllocation *allocation G_GNUC_UNUSED,
                                    gpointer pane)
{
  if (! text_size_allocated_idle_tag)
  {
    text_size_allocated_idle_tag =
      g_idle_add(text_size_allocated_idle_cb, pane);
  }
}

/***
****
***/

void BodyPane ::copy_url_cb(GtkMenuItem *mi G_GNUC_UNUSED, gpointer pane)
{
  static_cast<BodyPane *>(pane)->copy_url();
}

void BodyPane ::copy_url()
{
  gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),
                         _hover_url.c_str(),
                         _hover_url.size());
}

void BodyPane ::populate_popup_cb(GtkTextView *v, GtkMenu *m, gpointer pane)
{
  static_cast<BodyPane *>(pane)->populate_popup(v, m);
}

void BodyPane ::populate_popup(GtkTextView *v G_GNUC_UNUSED, GtkMenu *m)
{
  // menu separator comes first.
  GtkWidget *mi = gtk_menu_item_new();
  gtk_widget_show(mi);
  gtk_menu_shell_prepend(GTK_MENU_SHELL(m), mi);

  // then, on top of it, the suggestions menu.
  bool const copy_url_enabled = ! _hover_url.empty();
  mi = gtk_menu_item_new_with_mnemonic(_("Copy _URL"));
  g_signal_connect(mi, "activate", G_CALLBACK(copy_url_cb), this);
  gtk_widget_set_sensitive(mi, copy_url_enabled);
  gtk_widget_show_all(mi);
  gtk_menu_shell_prepend(GTK_MENU_SHELL(m), mi);
}

namespace {
// This function can be triggered:
//  - read a post with an attachment
//  - in message body, bottom left, right click on the icon below "Attachments"
static gboolean attachment_clicked_cb(GtkWidget *w, GdkEvent *event, gpointer p)
{
  BodyPane *bp = static_cast<BodyPane *>(p);
  gchar const *fn = (char *)g_object_get_data(G_OBJECT(w), "filename");
  if (! fn)
  {
    return TRUE;
  }

  bp->_current_attachment = fn;

  if (event->type == GDK_BUTTON_PRESS && event->button.button == 3)
  {
    gtk_menu_popup_at_pointer(GTK_MENU(bp->_menu), event);
  }

  return TRUE;
}
} // namespace

void BodyPane ::menu_clicked(MenuSelection const &ms)
{
  std::vector<Article> copies;
  copies.push_back(_article);
  GroupPrefs _group_prefs;
  SaveAttachmentsDialog *dialog = new SaveAttachmentsDialog(
    _prefs,
    _group_prefs,
    _data,
    _data,
    _cache,
    _data,
    _queue,
    get_window(_root),
    _header_pane->get_group(),
    copies,
    ms == MENU_SAVE_ALL ? TaskArticle::SAVE_ALL : TaskArticle::SAVE_AS,
    _current_attachment);
  gtk_widget_show(dialog->root());
}

void BodyPane ::menu_clicked_as_cb(GtkWidget *w, gpointer ptr)
{
  BodyPane *p = static_cast<BodyPane *>(ptr);
  if (! p)
  {
    return;
  }
  p->menu_clicked(MENU_SAVE_AS);
}

void BodyPane ::menu_clicked_all_cb(GtkWidget *w, gpointer ptr)
{
  BodyPane *p = static_cast<BodyPane *>(ptr);
  if (! p)
  {
    return;
  }
  p->menu_clicked(MENU_SAVE_ALL);
}

GtkWidget *BodyPane ::new_attachment(char const *filename)
{
  if (! filename)
  {
    return nullptr;
  }

  GtkWidget *w = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  GtkWidget *attachment = gtk_label_new(filename);
  GtkWidget *image =
    gtk_image_new_from_stock(GTK_STOCK_FILE, GTK_ICON_SIZE_MENU);

  gtk_label_set_selectable(GTK_LABEL(attachment), true);

  GtkWidget *event_box = gtk_event_box_new();
  gtk_container_add(GTK_CONTAINER(event_box), image);
  g_object_set_data(G_OBJECT(event_box), "filename", (gpointer)filename);

  gtk_box_pack_start(GTK_BOX(w), event_box, false, false, 0);
  gtk_box_pack_start(GTK_BOX(w), attachment, false, false, 0);

  g_signal_connect(
    event_box, "button_press_event", G_CALLBACK(attachment_clicked_cb), this);

  return w;
}

void BodyPane ::clear_attachments()
{
  _cur_col = 0;
  _cur_row = 0;
  _attachments = 0;
  _current_attachment = nullptr;

  {
    gtk_widget_set_no_show_all(_att_box, TRUE);
    gtk_widget_hide(_att_box);
    gtk_container_remove(GTK_CONTAINER(_att_box), _att_toolbar);
    _att_toolbar = nullptr;
    (void)create_attachments_toolbar(_att_box);
  }
}

/// FIXME : shows only half the icon on gtk2+, gtk3+ works fine. hm....
void BodyPane ::add_attachment_to_toolbar(char const *fn)
{
  if (! fn)
  {
    return;
  }

  GtkWidget *w = new_attachment(fn);
  //  gtk_widget_set_size_request(w, -1, 32);

  ++_attachments;
  if (_attachments % 4 == 0 && _attachments != 0)
  {
    gtk_grid_insert_row(GTK_GRID(_att_toolbar), ++_cur_row);
    _cur_col = 0;
  }

  gtk_grid_attach(GTK_GRID(_att_toolbar), w, _cur_col++, _cur_row, 1, 1);

  gtk_widget_set_no_show_all(_att_box, FALSE);
  gtk_widget_show_all(_att_box);
}

#ifdef HAVE_WEBKIT
void BodyPane ::set_html_text(const char *text)
{
  webkit_web_view_load_string(
    WEBKIT_WEB_VIEW(_web_view), text, nullptr, nullptr, "");
}
#endif

GtkWidget *BodyPane ::create_attachments_toolbar(GtkWidget *box)
{

  _cur_col = 0;
  _cur_row = 0;

  GtkWidget *w;
  w = _att_toolbar = gtk_grid_new();
  gtk_grid_insert_row(GTK_GRID(w), 0);
  gtk_grid_set_column_spacing(GTK_GRID(w), 3);
  gtk_grid_set_row_spacing(GTK_GRID(w), 4);
  gtk_container_add(GTK_CONTAINER(box), w);

  return box;
}

/***
****
***/

BodyPane ::BodyPane(Data &data,
                    ArticleCache &cache,
                    Prefs &prefs,
                    GroupPrefs &gp,
                    Queue &q,
                    HeaderPane *hp) :
  _prefs(prefs),
  _group_prefs(gp),
  _queue(q),
  _header_pane(hp),
  _data(data),
  _cache(cache),
#ifdef HAVE_WEBKIT
  _web_view(webkit_web_view_new()),
#endif
  _hscroll_visible(false),
  _vscroll_visible(false),
  _message(nullptr),
#ifdef HAVE_GMIME_CRYPTO
//  _gpgerr(GPG_DECODE),
#endif
  _attachments(0),
  _cleared(true),
  _current_attachment(nullptr)
{

  GtkWidget *w, *l, *hbox;

  for (guint i = 0; i < NUM_ICONS; ++i)
  {
    icons[i].pixbuf = load_icon(icons[i].pixbuf_file);
  }

  create_cursors();
  create_emoticons();

  // menu for popup menu for attachments
  _menu = gtk_menu_new();
  l = gtk_menu_item_new_with_label(_("Save Attachment As..."));
  g_signal_connect(l, "activate", G_CALLBACK(menu_clicked_as_cb), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(_menu), l);
  l = gtk_menu_item_new_with_label(_("Save All Attachments"));
  _selection = MENU_SAVE_ALL;
  g_signal_connect(l, "activate", G_CALLBACK(menu_clicked_all_cb), this);
  gtk_menu_shell_append(GTK_MENU_SHELL(_menu), l);
  gtk_widget_show_all(_menu);

  GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);

  // about this expander... getting the ellipsis to work is a strange process.
  // once you turn ellipsize on, the expander tries to make its label as narrow
  // as it can and just have the three "..."s.  So gtk_label_set_width_chars
  // is used to force labels to want to be the right size... but then they
  // never ellipsize.  But, if we start with gtk_widget_set_size_request() to
  // tell the expander that it _wants_ to be very small, then it will still take
  // extra space given to it by its parent without asking for enough size to
  // fit the entire label.
  w = _expander = gtk_expander_new(nullptr);
  gtk_widget_set_size_request(w, 50, -1);
  g_signal_connect(w, "activate", G_CALLBACK(expander_activated_cb), this);
  gtk_box_pack_start(GTK_BOX(vbox), w, false, false, 0);
  gtk_box_pack_start(GTK_BOX(vbox),
                     gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                     false,
                     false,
                     0);

  _terse = gtk_label_new("Expander");
  g_object_ref_sink(G_OBJECT(_terse));
  gtk_label_set_xalign(GTK_LABEL(_terse), 0.0f);
  gtk_label_set_yalign(GTK_LABEL(_terse), 0.5f);

  gtk_label_set_use_markup(GTK_LABEL(_terse), true);
  gtk_label_set_selectable(GTK_LABEL(_terse), true);
  gtk_label_set_ellipsize(GTK_LABEL(_terse), PANGO_ELLIPSIZE_MIDDLE);
  gtk_widget_show(_terse);
  g_signal_connect(
    _terse, "button-press-event", G_CALLBACK(verbose_clicked_cb), this);

  hbox = _verbose = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
  g_object_ref_sink(G_OBJECT(_verbose));
  w = _headers = gtk_label_new("Headers");
  gtk_label_set_selectable(GTK_LABEL(_headers), TRUE);
  gtk_label_set_xalign(GTK_LABEL(_headers), 0.0f);
  gtk_label_set_yalign(GTK_LABEL(_headers), 0.5f);
  gtk_label_set_ellipsize(GTK_LABEL(w), PANGO_ELLIPSIZE_MIDDLE);
  gtk_label_set_use_markup(GTK_LABEL(w), true);
  gtk_box_pack_start(GTK_BOX(hbox), w, true, true, PAD_SMALL);

  w = _xface = gtk_image_new();
  gtk_widget_set_size_request(w, 0, FACE_SIZE);
  gtk_box_pack_start(GTK_BOX(hbox), w, false, false, PAD_SMALL);
  w = _face = gtk_image_new();
  gtk_widget_set_size_request(w, 0, FACE_SIZE);
  gtk_box_pack_start(GTK_BOX(hbox), w, false, false, PAD_SMALL);
  gtk_widget_show_all(_verbose);
  g_signal_connect(
    _verbose, "button-press-event", G_CALLBACK(verbose_clicked_cb), this);
#ifdef HAVE_GMIME_CRYPTO
  gtk_widget_set_has_tooltip(_verbose, true);
  g_signal_connect(
    _verbose, "query-tooltip", G_CALLBACK(on_verbose_tooltip_cb), this);
#endif

  // setup
  _text = gtk_text_view_new();
  refresh_fonts();
  gtk_widget_add_events(_text,
                        GDK_POINTER_MOTION_MASK | GDK_POINTER_MOTION_HINT_MASK);
  gtk_container_set_border_width(GTK_CONTAINER(_text), PAD_SMALL);
  gtk_text_view_set_editable(GTK_TEXT_VIEW(_text), false);
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(_text), false);
  _scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(_scroll), _text);
  gtk_widget_show_all(vbox);
  gtk_box_pack_start(GTK_BOX(vbox), _scroll, true, true, 0);

  // add a toolbar for attachments
  _att_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  GtkWidget *att_label = gtk_label_new(_("Attachments:"));
  gtk_misc_set_padding(GTK_MISC(att_label), PAD_SMALL, 0);
  gtk_label_set_xalign(GTK_LABEL(att_label), 0.0f);
  gtk_label_set_yalign(GTK_LABEL(att_label), 0.0f);
  gtk_box_pack_start(GTK_BOX(_att_box),
                     gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                     false,
                     false,
                     0);
  gtk_box_pack_start(GTK_BOX(_att_box), att_label, false, false, 0);
  gtk_box_pack_start(
    GTK_BOX(vbox), create_attachments_toolbar(_att_box), false, false, 0);
  gtk_box_pack_start(GTK_BOX(vbox),
                     gtk_separator_new(GTK_ORIENTATION_HORIZONTAL),
                     false,
                     false,
                     0);

  // set up the buffer tags
  _buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(_text));
  set_text_buffer_tags(_buffer, _prefs);

  set_text_from_message(nullptr);
  bool const expanded(_prefs.get_flag("body-pane-headers-expanded", true));
  gtk_expander_set_expanded(GTK_EXPANDER(_expander), expanded);
  expander_activated_idle(this);

#ifdef HAVE_WEBKIT
  w = gtk_notebook_new();
  GtkNotebook *n(GTK_NOTEBOOK(w));
  gtk_notebook_append_page(n, vbox, gtk_label_new(_("Text View")));

  // add scroll to html
  GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
  gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(scroll),
                                      GTK_SHADOW_IN);
  gtk_scrolled_window_set_policy(
    GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add(GTK_CONTAINER(scroll), _web_view);
  gtk_notebook_append_page(n, scroll, gtk_label_new(_("HTML View")));
#else
  w = vbox;
#endif
  _root = w;
  _prefs.add_listener(this);

  // listen for user interaction
  g_signal_connect(
    _text, "motion_notify_event", G_CALLBACK(motion_notify_event), &_hover_url);
  g_signal_connect(
    _text, "button_press_event", G_CALLBACK(mouse_button_pressed_cb), this);
  g_signal_connect(
    _text, "key_press_event", G_CALLBACK(text_key_pressed), _scroll);
  g_signal_connect(
    _text, "size_allocate", G_CALLBACK(text_size_allocated), this);
  g_signal_connect(
    _text, "populate_popup", G_CALLBACK(populate_popup_cb), this);
  g_signal_connect(_root, "show", G_CALLBACK(show_cb), this);

  gtk_widget_show_all(_root);
}

BodyPane ::~BodyPane()
{
  _prefs.remove_listener(this);

  g_object_unref(_verbose);
  g_object_unref(_terse);

  if (_message)
  {
    g_object_unref(_message);
  }

  for (int i = 0; i < NUM_ICONS; ++i)
  {
    g_object_unref(icons[i].pixbuf);
  }

  foreach (std::set<char *>, _attach_names, it)
  {
    g_free(*it);
  }

  // store last opened message in prefs
  _prefs.set_string("last-opened-msg",
                    get_cleared() ? "" : get_message_id().to_view());
  free_emoticons();
  free_cursors();
}

namespace {
int const smooth_scrolling_speed(10);

void sylpheed_textview_smooth_scroll_do(GtkAdjustment *vadj,
                                        gfloat old_value,
                                        gfloat new_value,
                                        int step)
{
  bool const down(old_value < new_value);
  int const change_value =
    (int)(down ? new_value - old_value : old_value - new_value);
  for (int i = step; i <= change_value; i += step)
  {
    gtk_adjustment_set_value(vadj, old_value + (down ? i : -i));
  }
  gtk_adjustment_set_value(vadj, new_value);
}
} // namespace

bool BodyPane ::read_more_or_less(bool more)
{
  GtkWidget *parent = gtk_widget_get_parent(_text);
  GtkAdjustment *v =
    gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(parent));

  // figure out how far we scroll
  double const v_value = gtk_adjustment_get_value(v);
  double const v_upper = gtk_adjustment_get_upper(v);
  double const v_lower = gtk_adjustment_get_lower(v);
  double const v_page_size = gtk_adjustment_get_page_size(v);
  int const arbitrary_font_height_pixels_hack(18);
  float const inc(v_page_size - arbitrary_font_height_pixels_hack);
  gfloat const val(
    CLAMP(v_value + (more ? inc : -inc),
          v_lower,
          MAX(v_upper, v_page_size) - MIN(v_upper, v_page_size)));

  // if we can scroll, do so.
  bool handled(false);
  if (v_upper >= v_page_size && val != v_value)
  {
    if (_prefs.get_flag("smooth-scrolling", true))
    {
      sylpheed_textview_smooth_scroll_do(
        v, v_value, val, smooth_scrolling_speed);
    }
    else
    {
      gtk_adjustment_set_value(v, val);
    }

    handled = true;
  }

  return handled;
}

namespace {
// (1) strip redundant leading Re: and RE:
// (2) ensure the remaining Re: has a lowercase e

std::string normalize_subject_re(StringView const &v_in)
{
  StringView v(v_in), prev(v_in);
  while (! v.empty())
  {
    v.ltrim();
    StringView tmp(v);
    if (tmp.strstr("Re:") == tmp.str)
    {
      tmp.eat_chars(3);
    }
    else if (v.strstr("RE:") == tmp.str)
    {
      tmp.eat_chars(3);
    }
    else
    {
      break;
    }
    prev = v;
    v = tmp;
  }

  std::string ret(prev.str, prev.len);
  if (! ret.find("RE:")) // force lowercase 'e'
  {
    ret.replace(0, 3, "Re:");
  }

  return ret;
}

std::string get_header(GMimeMessage *msg,
                       char const *key,
                       char const *fallback_charset_1,
                       char const *fallback_charset_2)
{
  StringView const v(g_mime_object_get_header((GMimeObject *)msg, key));
  std::string s;
  if (! v.empty())
  {
    s = header_to_utf8(v, fallback_charset_1, fallback_charset_2);
  }
  return s;
}

struct ForeachPartData
{
    std::string fallback_charset;
    std::string body;
};

void get_utf8_body_foreach_part(GMimeObject * /*parent*/,
                                GMimeObject *o,
                                gpointer user_data)
{
  GMimePart *part;
  GMimeContentType *type = g_mime_object_get_content_type(o);
  bool const is_text(g_mime_content_type_is_type(type, "text", "*"));
  if (is_text)
  {
    part = GMIME_PART(o);
    ForeachPartData *data(static_cast<ForeachPartData *>(user_data));
    data->body += mime_part_to_utf8(part, data->fallback_charset.c_str());
  }
}

std::string get_utf8_body(GMimeMessage *source, char const *fallback_charset)
{
  ForeachPartData tmp;
  if (fallback_charset)
  {
    tmp.fallback_charset = fallback_charset;
  }
  if (source)
  {
    g_mime_message_foreach(source, get_utf8_body_foreach_part, &tmp);
  }
  return tmp.body;
}
} // namespace

GMimeMessage *BodyPane ::create_followup_or_reply(bool is_reply)
{
  GMimeMessage *msg(nullptr);

  if (_message)
  {
    msg = g_mime_message_new(false);
    GMimeObject *msg_obj = (GMimeObject *)msg;
    GMimeObject *_message_obj = (GMimeObject *)_message;

    // fallback character encodings
    char const *group_charset(_charset.c_str());
    GMimeContentType *type(
      g_mime_object_get_content_type(GMIME_OBJECT(_message)));
    char const *message_charset(
      type ? g_mime_content_type_get_parameter(type, "charset") : nullptr);

    ///
    ///  HEADERS
    ///

    // To:, Newsgroups:
    std::string const from(
      get_header(_message, "From", message_charset, group_charset));
    std::string const newsgroups(
      get_header(_message, "Newsgroups", message_charset, group_charset));
    std::string const fup_to(
      get_header(_message, "Followup-To", message_charset, group_charset));
    std::string const reply_to(
      get_header(_message, "Reply-To", message_charset, group_charset));

    if (is_reply || fup_to == "poster")
    {
      std::string const &to(reply_to.empty() ? from : reply_to);
      pan_g_mime_message_add_recipients_from_string(
        msg, GMIME_ADDRESS_TYPE_TO, to.c_str());
    }
    else
    {
      std::string const &groups(fup_to.empty() ? newsgroups : fup_to);
      g_mime_object_append_header(
        (GMimeObject *)msg, "Newsgroups", groups.c_str(), nullptr);
    }

    // Subject:
    StringView v = g_mime_object_get_header(_message_obj, "Subject");
    std::string h = header_to_utf8(v, message_charset, group_charset);
    std::string val(normalize_subject_re(h));
    if (val.find("Re:") != 0) // add "Re: " if we don't have one
    {
      val.insert(0, "Re: ");
    }
    g_mime_message_set_subject(msg, val.c_str(), nullptr);

    // attribution lines
    char const *cpch = g_mime_object_get_header(_message_obj, "From");
    h = header_to_utf8(cpch, message_charset, group_charset);
    g_mime_object_append_header(
      msg_obj, "X-Draft-Attribution-Author", h.c_str(), nullptr);

    cpch = g_mime_message_get_message_id(_message);
    h = header_to_utf8(cpch, message_charset, group_charset);
    g_mime_object_append_header(
      msg_obj, "X-Draft-Attribution-Id", h.c_str(), nullptr);

    char const *header_t = "Date";
    char const *tmp_s = g_mime_object_get_header(_message_obj, header_t);
    char const *tmp = tmp_s; // FIXME: convert time to string
    h = header_to_utf8(tmp, message_charset, group_charset);
    g_mime_object_append_header(
      msg_obj, "X-Draft-Attribution-Date", h.c_str(), nullptr);

    // references
    char const *header = "References";
    v = g_mime_object_get_header(_message_obj, header);
    val.assign(v.str, v.len);
    if (! val.empty())
    {
      val += ' ';
    }
    val += "<";
    val += g_mime_message_get_message_id(_message);
    val += ">";
    val = GNKSA ::trim_references(val);
    g_mime_object_append_header(msg_obj, header, val.c_str(), nullptr);

    ///
    ///  BODY
    ///

    GtkTextIter start, end;
    if (gtk_text_buffer_get_selection_bounds(_buffer, &start, &end))
    {
      // go with the user-selected region w/o modifications.
      // since it's in the text pane it's already utf-8...
      h = gtk_text_buffer_get_text(_buffer, &start, &end, false);
    }
    else
    {
      // get the body; remove the sig
      h = get_utf8_body(_message, group_charset);
      StringView v(h);
      int sig_index(0);
      if (GNKSA::find_signature_delimiter(h, sig_index) != GNKSA::SIG_NONE)
      {
        v.len = sig_index;
      }
      v.trim();
      h = std::string(v.str, v.len);
    }

    // quote the body
    std::string s;
    for (char const *c(h.c_str()); c && *c; ++c)
    {
      if (c == h.c_str() || c[-1] == '\n')
      {
        s += (*c == '>' ? ">" : "> ");
      }
      s += *c;
    }

    // set the clone's content object with our modified body
    GMimeStream *stream = g_mime_stream_mem_new();
    g_mime_stream_write_string(stream, s.c_str());
    GMimeDataWrapper *wrapper =
      g_mime_data_wrapper_new_with_stream(stream, GMIME_CONTENT_ENCODING_8BIT);
    GMimePart *part = g_mime_part_new();
    GMimeContentType *new_type =
      g_mime_content_type_parse(nullptr, "text/plain; charset=UTF-8");
    g_mime_object_set_content_type((GMimeObject *)part, new_type);
    g_mime_part_set_content(part, wrapper);
    g_mime_part_set_content_encoding(part, GMIME_CONTENT_ENCODING_8BIT);
    g_mime_message_set_mime_part(msg, GMIME_OBJECT(part));
    g_object_unref(new_type);
    g_object_unref(wrapper);
    g_object_unref(part);
    g_object_unref(stream);

    // std::cerr << LINE_ID << " here is the modified clone\n [" <<
    // g_mime_object_to_string((GMimeObject *)msg) << ']' << std::endl;
  }

  return msg;
}

/***
****
***/

void BodyPane ::refresh_fonts()
{
  bool const body_pane_font_enabled =
    _prefs.get_flag("body-pane-font-enabled", false);
  bool const monospace_font_enabled =
    _prefs.get_flag("monospace-font-enabled", false);

  if (! body_pane_font_enabled && ! monospace_font_enabled)
  {
    gtk_widget_override_font(_text, nullptr);
  }
  else
  {
    std::string const str(
      monospace_font_enabled ?
        _prefs.get_string("monospace-font", "Monospace 10") :
        _prefs.get_string("body-pane-font", "Sans 10"));
    PangoFontDescription *pfd(pango_font_description_from_string(str.c_str()));
    gtk_widget_override_font(_text, pfd);
    pango_font_description_free(pfd);
  }
}

void BodyPane ::on_prefs_flag_changed(StringView const &key,
                                      bool value G_GNUC_UNUSED)
{
  if ((key == "body-pane-font-enabled") || (key == "monospace-font-enabled"))
  {
    refresh_fonts();
  }

  if ((key == "wrap-article-body") || (key == "mute-quoted-text")
      || (key == "mute-signature") || (key == "show-smilies-as-graphics")
      || (key == "show-all-headers") || (key == "size-pictures-to-fit")
      || (key == "show-text-markup") || (key == "highlight-urls")
      || (key == "show-article-sig"))
  {
    refresh();
  }
}

void BodyPane ::on_prefs_string_changed(StringView const &key,
                                        StringView const &value G_GNUC_UNUSED)
{
  if ((key == "body-pane-font") || (key == "monospace-font"))
  {
    refresh_fonts();
  }
}

void BodyPane ::on_prefs_color_changed(StringView const &key,
                                       GdkRGBA const &color G_GNUC_UNUSED)
{
  if ((key == "text-color-fg") || (key == "text-color-bg")
      || (key == "body-pane-color-url") || (key == "body-pane-color-url-bg")
      || (key == "body-pane-color-quote-1")
      || (key == "body-pane-color-quote-2")
      || (key == "body-pane-color-quote-3")
      || (key == "body-pane-color-quote-1-bg")
      || (key == "body-pane-color-quote-2-bg")
      || (key == "body-pane-color-quote-3-bg")
      || (key == "body-pane-color-signature")
      || (key == "body-pane-color-signature-bg"))
  {
    refresh_colors();
  }
}

void BodyPane ::refresh_colors()
{
  set_text_buffer_tags(_buffer, _prefs);
  set_text_from_message(_message);
}

void BodyPane ::set_character_encoding(char const *charset)
{
  if (charset && *charset)
  {
    _charset = charset;
  }
  else
  {
    _charset.clear();
  }

  refresh();
}

} // namespace pan
