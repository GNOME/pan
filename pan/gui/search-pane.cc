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
  #include "gtk-compat.h"
#ifdef HAVE_RSS
  #include <libgrss.h>
  #include <libsoup/soup.h>
#endif
}

#include "search-pane.h"
#include "searchpane.ui.h"
#include <pan/general/macros.h>
#include <pan/gui/pad.h>
#include <pan/general/file-util.h>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <locale.h>

using namespace pan;

namespace
{
  const EvolutionDateMaker date_maker;
}

FeedItem :: FeedItem (const char* feed_title, const char* descr, const char* item_url, time_t time_str):
    title(feed_title), desc(descr), url(item_url), date(date_maker.get_date_string(time_str)) {
}

namespace
{
  GrssFeedChannel *feed(0);

  std::string escaped (const std::string& s)
  {
    char * pch = g_markup_escape_text (s.c_str(), s.size());
    const std::string ret (pch);
    g_free (pch);
    return ret;
  }

  namespace
  {

    std::string sort_order ("age");
    int page (0);

    void order_mode_changed_cb (GtkComboBox * c, gpointer user_data)
    {

      // reset page, we want to start fresh
      page = 0;

      SearchPane * pane (static_cast<SearchPane*>(user_data));

      const int column (1);
      const int row (gtk_combo_box_get_active (c));
      GtkTreeModel * m = gtk_combo_box_get_model (c);
      GtkTreeIter i;
      if (gtk_tree_model_iter_nth_child (m, &i, 0, row)) {
        char * val (0);
        gtk_tree_model_get (m, &i, column, &val, -1);
        sort_order = val;
        pane->refresh();
        g_free (val);
      }
    }


    GtkWidget* create_search_order_combo_box (SearchPane* pane)
    {

      const char* strings[4][2] =
      {
        //WTF nzbindex....
        {N_("Age (newest first)"), "agedesc"},
        {N_("Age (oldest first)"), "age"},
        {N_("Size (smallest first)"), "size"},
        {N_("Size (largest first)"), "sizedesc"}
      };

      GtkListStore * store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

      for (size_t i=0; i<G_N_ELEMENTS(strings); ++i) {
        GtkTreeIter iter;
        gtk_list_store_append (store, &iter);
        gtk_list_store_set (store, &iter, 0, strings[i][0], 1, strings[i][1], -1);
      }

      GtkWidget * c = gtk_combo_box_new_with_model (GTK_TREE_MODEL(store));
      GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
      gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (c), renderer, true);
      gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (c), renderer, "text", 0, NULL);
      gtk_combo_box_set_active (GTK_COMBO_BOX(c), 0);
      g_signal_connect (c, "changed", G_CALLBACK(order_mode_changed_cb), pane);

      gtk_widget_show_all(c);

      return c;
    }
  }

  void init_feed(const std::string& query, std::vector<FeedItem*>& fillme)
  {
    std::stringstream str;
    str << "http://www.nzbindex.nl/rss/?q=";
    str << escaped(query)<<"&max=600&age=550&hidespam=1&nzblink=1";
    str << "&sort="<<sort_order;

    //add two cookies to session to skip nzbindex's agreement screen and to set language to english
    GrssFeedChannel* feed = grss_feed_channel_new_with_source((char*)str.str().c_str());
    SoupCookie * cookie = soup_cookie_new ("agreed", "true", "www.nzbindex.nl", "/", -1);
    grss_feed_channel_add_cookie (feed, cookie);
    cookie = soup_cookie_new ("lang", "2", "www.nzbindex.nl", "/", -1);
    grss_feed_channel_add_cookie (feed, cookie);

    GList* list = grss_feed_channel_fetch_all (feed, 0);
    GList *iter;
    for (iter = list; iter; iter = g_list_next (iter))
    {
        GrssFeedItem* item = (GrssFeedItem*)iter->data;
        FeedItem* fitem = new FeedItem(grss_feed_item_get_title(item),
            grss_feed_item_get_description(item),
            grss_feed_item_get_source(item),
            grss_feed_item_get_publish_time(item));
        fillme.push_back(fitem);
    }
  }
}

SearchPane :: ~SearchPane ()
{}

namespace
{
  const char* download_file(const std::string& filename)
  {
    SoupMessage *msg;
    SoupSession* session = soup_session_sync_new();
    msg = soup_message_new (SOUP_METHOD_GET, filename.c_str());
    soup_session_send_message (session, msg);
    return msg->response_body->data;
  }
}

void SearchPane :: download_clicked_cb (GtkButton*, SearchPane* pane)
{
  FeedItem* sel = pane->get_selection ();
  if (sel)
    pane->_gui.do_import_tasks_from_nzb_stream(download_file(sel->url));
}

void SearchPane :: refresh_clicked_cb (GtkButton*, SearchPane* pane)
{
  pane->refresh();
}

void SearchPane :: page_clicked_cb (GtkButton* b, SearchPane* pane)
{
  gchar* val = (gchar*)g_object_get_data(G_OBJECT(b), "direction");
  if (!strcmp(val, "next"))
      ++page;
  if (!strcmp(val, "prev"))
  {
      --page;
      if (page<0) page = 0;
  }
  pane->refresh();
}

namespace
{
  enum
  {
    S_COL_NAME,
    S_COL_DATE,
    S_NUM_COLS
  };
}

void
SearchPane :: get_selected_downloads_foreach (GtkTreeModel * model,
                                            GtkTreePath  * ,
                                            GtkTreeIter  * iter,
                                            gpointer       data)
{
  FeedItem* name;
  gtk_tree_model_get (model, iter, S_COL_NAME, &name, -1);
  static_cast<download_v*>(data)->push_back (name);
}

FeedItem*
SearchPane :: get_selection()
{
  GtkTreeView * view (GTK_TREE_VIEW (_view));
  GtkTreeSelection * sel (gtk_tree_view_get_selection (view));
  download_v downloads;
  gtk_tree_selection_selected_foreach (sel, get_selected_downloads_foreach, &downloads);
  if (downloads.size()==0) return 0;
  return downloads.front();
}

void
SearchPane::  do_popup_menu (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{
  SearchPane * self (static_cast<SearchPane*>(userdata));
  GtkWidget * menu (gtk_ui_manager_get_widget (self->_uim, "/searchpane-popup"));
  gtk_menu_popup (GTK_MENU(menu), NULL, NULL, NULL, NULL,
                  (event ? event->button : 0),
                  (event ? event->time : 0));
}

namespace
{
  void do_download  (GtkAction*, gpointer p)  { static_cast<SearchPane*>(p)->download_clicked_cb(0, static_cast<SearchPane*>(p)); }

  void replaceAll(std::string& str, const std::string& from, const std::string& to) {
      if(from.empty())
          return;
      size_t start_pos = 0;
      while((start_pos = str.find(from, start_pos)) != std::string::npos) {
          str.replace(start_pos, from.length(), to);
          start_pos += to.length();
      }
  }

  GtkActionEntry searchpane_popup_entries[] =
  {

    { "download", NULL,
      N_("Download"), "",
      N_("Download"),
      G_CALLBACK(do_download) }
  };

  void ellipsize_if_supported (GObject * o)
  {
    g_object_set (o, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  }

  void render_name (GtkTreeViewColumn * ,
                    GtkCellRenderer   * renderer,
                    GtkTreeModel      * model,
                    GtkTreeIter       * iter,
                    gpointer            userdata)
  {

    //TODO strip a href tags!
    FeedItem* text (0);
    gtk_tree_model_get (model, iter, S_COL_NAME, &text, -1);
    std::string str(text->desc);
    replaceAll(str, "\r\n", "");
    replaceAll(str, "\n", "");
    replaceAll(str, "<br/>", "\n");
    replaceAll(str, "<p>", "");
    replaceAll(str, "</p>", "");
    replaceAll(str, "</div>", "");
    replaceAll(str, "<font color=", "<span color=");
    replaceAll(str, "</font>", "</span>");
    replaceAll(str, "<div xmlns=\"http://www.w3.org/1999/xhtml\">", "");
    std::stringstream stream;
    stream << text->title << "\n" << str;
    g_object_set (renderer, "markup", stream.str().c_str(), NULL);
  }

  gboolean on_popup_menu (GtkWidget * treeview, gpointer userdata)
  {
    SearchPane::do_popup_menu (treeview, NULL, userdata);
    return true;
  }
}

void
SearchPane :: add_actions (GtkWidget * box)
{
  // action manager for popup
  _uim = gtk_ui_manager_new ();
  // read the file...
  char * filename = g_build_filename (file::get_pan_home().c_str(), "searchpane.ui", NULL);
  GError * err (0);
  if (!gtk_ui_manager_add_ui_from_file (_uim, filename, &err)) {
    g_clear_error (&err);
    gtk_ui_manager_add_ui_from_string (_uim, fallback_searchpane_ui, -1, &err);
  }
  if (err) {
    Log::add_err_va (_("Error reading file \"%s\": %s"), filename, err->message);
    g_clear_error (&err);

  }
  g_free (filename);

  //add popup actions
  GtkActionGroup* pgroup = gtk_action_group_new ("SearchPane");
  gtk_action_group_set_translation_domain (pgroup, NULL);
  gtk_action_group_add_actions (pgroup, searchpane_popup_entries, G_N_ELEMENTS(searchpane_popup_entries), this);
  gtk_ui_manager_insert_action_group (_uim, pgroup, 0);

}

gboolean
SearchPane :: on_button_pressed (GtkWidget *treeview, GdkEventButton *event, gpointer userdata)
{

  if (event->type == GDK_BUTTON_PRESS )
  {
    GtkTreeSelection *selection;
    selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(treeview));

    if ( event->button == 3)
    {

      if (gtk_tree_selection_count_selected_rows(selection)  <= 1)
      {
         GtkTreePath *path;
         /* Get tree path for row that was clicked */
         if (gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(treeview),
                                           (gint) event->x,
                                           (gint) event->y,
                                           &path, NULL, NULL, NULL))
         {
           gtk_tree_selection_unselect_all(selection);
           gtk_tree_selection_select_path(selection, path);
           gtk_tree_path_free(path);
         }
      }
      do_popup_menu(treeview, event, userdata);
      return true;
    }

  }
  return false;
}

namespace
{
  GtkWidget * add_button (GtkWidget   * box,
                                      const gchar * label,
                                      GCallback     callback,
                                      gpointer      user_data)
  {
    GtkWidget * w = gtk_button_new_with_label(label);
    gtk_button_set_relief (GTK_BUTTON(w), GTK_RELIEF_NONE);
    if (callback)
      g_signal_connect (w, "clicked", callback, user_data);
    gtk_box_pack_start (GTK_BOX(box), w, false, false, 0);
    return w;
  }
}

namespace
{
  // the text typed by the user.
  std::string search_text;

  guint entry_changed_tag (0u);
  guint activate_soon_tag (0u);

  // name, regex
  int search_mode;

  void set_search_entry (GtkWidget * entry, const char * s)
  {
    g_signal_handler_block (entry, entry_changed_tag);
    gtk_entry_set_text (GTK_ENTRY(entry), s);
    g_signal_handler_unblock (entry, entry_changed_tag);
  }

  gboolean search_entry_focus_in_cb (GtkWidget     * w,
                                     GdkEventFocus * ,
                                     gpointer        )
  {
#if !GTK_CHECK_VERSION(3,0,0)
    gtk_widget_modify_text (w, GTK_STATE_NORMAL, NULL); // resets
#else
    gtk_widget_override_color (w, GTK_STATE_FLAG_NORMAL, NULL);
#endif
    set_search_entry (w, search_text.c_str());
    return false;
  }

  const char * mode_strings [] =
  {
    N_("Name"),
    N_("Name (regex)"),
  };

  enum
  {
    SUBJECT_OR_AUTHOR=0,
    SUBJECT_OR_AUTHOR_REGEX=1
  };

  void refresh_search_entry (GtkWidget * w)
  {
    if (search_text.empty() && !gtk_widget_has_focus(w))
    {
#if !GTK_CHECK_VERSION(3,0,0)
      GdkColor c;
      c.pixel = 0;
      c.red = c.green = c.blue = 0xAAAA;
      gtk_widget_modify_text (w, GTK_STATE_NORMAL, &c);
#else
      GdkRGBA c;
      gdk_rgba_parse (&c, "0xAAA");
      gtk_widget_override_color(w, GTK_STATE_FLAG_NORMAL, &c);
#endif
      set_search_entry (w, _(mode_strings[search_mode]));
    }
  }

  gboolean search_entry_focus_out_cb (GtkWidget     * w,
                                      GdkEventFocus * ,
                                      gpointer        )
  {
    refresh_search_entry (w);
    return false;
  }

  void search_activate (SearchPane * h)
  {
    h->filter (search_text, search_mode);
  }

  void remove_activate_soon_tag ()
  {
    if (activate_soon_tag != 0)
    {
      g_source_remove (activate_soon_tag);
      activate_soon_tag = 0;
    }
  }

  void search_entry_activated (GtkEntry *, gpointer h_gpointer)
  {
    search_activate (static_cast<SearchPane*>(h_gpointer));
    remove_activate_soon_tag ();
  }

  gboolean activated_timeout_cb (gpointer h_gpointer)
  {
    search_activate (static_cast<SearchPane*>(h_gpointer));
    remove_activate_soon_tag ();
    return false; // remove the source
  }

  // ensure there's exactly one activation timeout
  // and that it's set to go off in a half second from now.
  void bump_activate_soon_tag (SearchPane * h)
  {
    remove_activate_soon_tag ();
    activate_soon_tag = g_timeout_add (500, activated_timeout_cb, h);
  }

  // when the user changes the filter text,
  // update our state variable and bump the activate timeout.
  void search_entry_changed (GtkEditable * e, gpointer h_gpointer)
  {
    search_text = gtk_entry_get_text (GTK_ENTRY(e));
    bump_activate_soon_tag (static_cast<SearchPane*>(h_gpointer));
    refresh_search_entry (GTK_WIDGET(e));
  }

  // when the search mode is changed via the menu,
  // update our state variable and bump the activate timeout.
  void search_menu_toggled_cb (GtkCheckMenuItem  * menu_item,
                                gpointer            entry_g)
  {
    if (gtk_check_menu_item_get_active  (menu_item))
    {
      search_mode = GPOINTER_TO_INT (g_object_get_data (G_OBJECT(menu_item), "MODE"));
      refresh_search_entry (GTK_WIDGET(entry_g));
      SearchPane * h = (SearchPane*) g_object_get_data (G_OBJECT(entry_g), "pane");
      bump_activate_soon_tag (h);
    }
  }

  void entry_icon_release (GtkEntry*, GtkEntryIconPosition icon_pos, GdkEventButton*, gpointer menu)
  {
    if (icon_pos == GTK_ENTRY_ICON_PRIMARY)
      gtk_menu_popup (GTK_MENU(menu), 0, 0, 0, 0, 0, gtk_get_current_event_time());
  }

  void entry_icon_release_2 (GtkEntry *entry, GtkEntryIconPosition icon_pos, GdkEventButton*, gpointer pane_gpointer)
  {
    if (icon_pos == GTK_ENTRY_ICON_SECONDARY) {
      set_search_entry (GTK_WIDGET(entry), "");
      refresh_search_entry (GTK_WIDGET(entry));
      search_text.clear ();
      search_entry_activated (NULL, pane_gpointer);
    }
  }

  gboolean filter_visible_func (GtkTreeModel *model, GtkTreeIter *iter, gpointer gdata)
  {

     if (search_text.empty()) return true;

     std::string search (search_text);
     std::transform(search.begin(), search.end(), search.begin(), ::tolower);

     FeedItem *task(0);
     /* Get value from column */
     gtk_tree_model_get( GTK_TREE_MODEL(model), iter, S_COL_NAME, &task, -1 );

     if (task)
     {
       std::string s1("");
       if (search_mode == 0)
       {
        s1 = task->title;
        std::transform(s1.begin(), s1.end(), s1.begin(), ::tolower);
        if (s1.find(search) != s1.npos) return true;
       }
       if (search_mode == 1)
       {
          GRegexCompileFlags cf0((GRegexCompileFlags)0);
          GRegexMatchFlags mf0((GRegexMatchFlags)0);
          GRegex* rex = g_regex_new (search.c_str(), cf0, mf0, NULL);
          if (!rex) return false;
          const bool match (g_regex_match (rex, task->title.c_str(), G_REGEX_MATCH_NOTEMPTY, NULL));
          g_regex_unref(rex);
          return match;
       }
     }

     return false;

  }
}

void
SearchPane :: filter (const std::string& text, int mode)
{
  search_text = text;
  search_mode = mode;
  gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER(gtk_tree_view_get_model(GTK_TREE_VIEW(_view))));
}

GtkWidget*
SearchPane :: create_filter_entry ()
{
  GtkWidget * entry = gtk_entry_new ();
  _action_manager.disable_accelerators_when_focused (entry);
  g_object_set_data (G_OBJECT(entry), "pane", this);
  g_signal_connect (entry, "focus-in-event", G_CALLBACK(search_entry_focus_in_cb), NULL);
  g_signal_connect (entry, "focus-out-event", G_CALLBACK(search_entry_focus_out_cb), NULL);
  g_signal_connect (entry, "activate", G_CALLBACK(search_entry_activated), this);
  entry_changed_tag = g_signal_connect (entry, "changed", G_CALLBACK(search_entry_changed), this);

  gtk_entry_set_icon_from_stock( GTK_ENTRY( entry ),
                                 GTK_ENTRY_ICON_PRIMARY,
                                 GTK_STOCK_FIND);
  gtk_entry_set_icon_from_stock( GTK_ENTRY( entry ),
                                 GTK_ENTRY_ICON_SECONDARY,
                                 GTK_STOCK_CLEAR );

  GtkWidget * menu = gtk_menu_new ();
  search_mode = 0;
  GSList * l = 0;
  for (int i=0, qty=G_N_ELEMENTS(mode_strings); i<qty; ++i) {
    GtkWidget * w = gtk_radio_menu_item_new_with_label (l, _(mode_strings[i]));
    l = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM(w));
    g_object_set_data (G_OBJECT(w), "MODE", GINT_TO_POINTER(i));
    g_signal_connect (w, "toggled", G_CALLBACK(search_menu_toggled_cb),entry);
    if (search_mode == i)
      gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM(w), TRUE);
    gtk_menu_shell_append (GTK_MENU_SHELL(menu), w);
    gtk_widget_show (w);
  }
  g_signal_connect (entry, "icon-release", G_CALLBACK(entry_icon_release), menu);
  g_signal_connect (entry, "icon-release", G_CALLBACK(entry_icon_release_2), this);

  refresh_search_entry (entry);

  return entry;
}

SearchPane :: SearchPane(Data                & data,
                         Queue               & queue,
                         GUI                 & gui,
                         ActionManager       & manager):
  _data (data),
  _queue (queue),
  _gui(gui),
  _action_manager(manager),
  _root(0),
  _store(0)
{

  _store = gtk_list_store_new (S_NUM_COLS, G_TYPE_POINTER, G_TYPE_STRING);
  _view = gtk_tree_view_new_with_model (GTK_TREE_MODEL(_store));

  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW(_view), true);
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW(_view), true);
  GtkCellRenderer* renderer(0);
  GtkTreeViewColumn* col (0);

  renderer = gtk_cell_renderer_text_new ();
  ellipsize_if_supported (G_OBJECT(renderer));
  col = gtk_tree_view_column_new_with_attributes (_("Release Info"), renderer, NULL);
  gtk_tree_view_column_set_cell_data_func(col, renderer, (GtkTreeCellDataFunc) render_name, &_queue, 0);
  gtk_tree_view_column_set_resizable (col, true);
  gtk_tree_view_insert_column(GTK_TREE_VIEW(_view), col, S_COL_NAME);

  renderer = gtk_cell_renderer_text_new ();
  col = gtk_tree_view_column_new_with_attributes (_("Date"), renderer, "text", S_COL_DATE, NULL);
  gtk_tree_view_column_set_resizable (col, true);
  gtk_tree_view_insert_column(GTK_TREE_VIEW(_view), col, S_COL_DATE);

  GtkWidget * scroll = gtk_scrolled_window_new (0, 0);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_container_add (GTK_CONTAINER(scroll), _view);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(scroll), GTK_SHADOW_IN);

  GtkWidget * buttons = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
  GtkWidget* w(0);
  w = add_button(buttons, _("Download"), G_CALLBACK(download_clicked_cb), this);
  gtk_widget_set_tooltip_text(w, _("Download selected Release/Files"));

  w = add_button(buttons, _("Refresh"), G_CALLBACK(refresh_clicked_cb), this);
  gtk_widget_set_tooltip_text(w, _("Refresh current results"));

  //TODO dbg!
  /*
  w = add_button(buttons, _("Next Page"), G_CALLBACK(page_clicked_cb), this);
  g_object_set_data_full (G_OBJECT(w), "direction", g_strdup("next"), g_free);
  w = add_button(buttons, _("Prev Page"), G_CALLBACK(page_clicked_cb), this);
  g_object_set_data_full (G_OBJECT(w), "direction", g_strdup("prev"), g_free);
  */

  GtkWidget * hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, PAD);
  gtk_box_pack_start(GTK_BOX(hbox), buttons, false, false, PAD);

  // search filter
  gtk_box_pack_start(GTK_BOX(hbox), gtk_separator_new(GTK_ORIENTATION_VERTICAL),
      0, 0, 0);
  gtk_box_pack_start(GTK_BOX(hbox), create_filter_entry(), false, false, PAD);

  // combo box for sort order
  w = create_search_order_combo_box(this);
  gtk_box_pack_start(GTK_BOX(hbox), w, false, true, PAD);

  GtkTreeModel* initial_model = gtk_tree_view_get_model(
      GTK_TREE_VIEW( _view ) );
  GtkTreeModel* filter_model = gtk_tree_model_filter_new(initial_model, NULL);
  gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER ( filter_model ),
      (GtkTreeModelFilterVisibleFunc) filter_visible_func, NULL, NULL);
  gtk_tree_view_set_model(GTK_TREE_VIEW( _view ), filter_model);
  g_object_unref(filter_model);

  GtkWidget * vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_box_pack_start(GTK_BOX(vbox), scroll, true, true, 0);
  gtk_box_pack_start(GTK_BOX(vbox), hbox, false, false, PAD);

  _root = vbox;
  gtk_widget_show_all(_root);

  // connect signals for popup menu
  g_signal_connect(_view, "popup-menu", G_CALLBACK(on_popup_menu), this);
  g_signal_connect(_view, "button-press-event", G_CALLBACK(on_button_pressed), this);

  add_actions(_view);

}

namespace
{
  std::vector<FeedItem*> items;
}

void
SearchPane::refresh()
{
  if (!search_text.empty())
  {

    foreach (download_v, items, it)
      delete *it;
    items.clear();
    init_feed(search_text, items);

    if (items.size()!=0)
    {
      GtkTreeModel* model = gtk_tree_view_get_model(GTK_TREE_VIEW(_view));
      g_object_ref(model);
      gtk_tree_view_set_model(GTK_TREE_VIEW(_view), NULL);
      gtk_list_store_clear(_store);
      GtkTreeIter iter;
      foreach (download_v, items, it)
      {
        gtk_list_store_append(_store, &iter);
        gtk_list_store_set (_store, &iter, S_COL_NAME, *it, S_COL_DATE, (*it)->date.c_str(), -1);
      }
      gtk_tree_view_set_model (GTK_TREE_VIEW(_view), model);
    }

    gtk_tree_view_scroll_to_point(GTK_TREE_VIEW(_view), 0, 0);
  }
}


