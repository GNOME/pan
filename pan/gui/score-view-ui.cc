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
#include "pad.h"
#include "score-add-ui.h"
#include "score-view-ui.h"

#if !GTK_CHECK_VERSION(2,6,0)
#define GTK_STOCK_EDIT GTK_STOCK_OPEN
#endif

using namespace pan;

ScoreView :: ~ScoreView ()
{
}

namespace
{
  void                                          
  response_cb (GtkDialog * dialog, int response, gpointer data)
  {
    if (response == GTK_RESPONSE_APPLY)
      static_cast<Data*>(data)->rescore ();

    gtk_widget_destroy (GTK_WIDGET(dialog));     
  }

  enum
  {
    COLUMN_SCORE_DELTA,
    COLUMN_SCORE_VALUE,
    COLUMN_CRITERIA,
    COLUMN_DATA,
    COLUMN_QTY
  };

  GtkWidget* create_rescore_button ()
  {
    GtkWidget * button = gtk_button_new ();
    GtkWidget * label = gtk_label_new_with_mnemonic (_("Close and Re_score"));
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (button));
			      
    GtkWidget * image = gtk_image_new_from_stock (GTK_STOCK_CLOSE, GTK_ICON_SIZE_BUTTON);
    GtkWidget * image2 = gtk_image_new_from_stock (GTK_STOCK_REFRESH, GTK_ICON_SIZE_BUTTON);
    GtkWidget * hbox = gtk_hbox_new (FALSE, 2);

    GtkWidget * align = gtk_alignment_new (0.5, 0.5, 0.0, 0.0);
					        
    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), image2, FALSE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    gtk_container_add (GTK_CONTAINER (button), align);
    gtk_container_add (GTK_CONTAINER (align), hbox);
    gtk_widget_show_all (align);

    return button;
  }

  void delete_score_view (gpointer score_view_gpointer)
  {
    delete (ScoreView*) score_view_gpointer;
  }

}

void
ScoreView :: add_destroy_cb (GtkObject* unused, gpointer view_gpointer)
{
  static_cast<ScoreView*>(view_gpointer)->tree_view_refresh ();
}
void
ScoreView :: on_add ()
{
  ScoreAddDialog * add = new ScoreAddDialog (_data, _root, _group, _article, ScoreAddDialog::ADD);
  g_signal_connect (add->root(), "destroy", G_CALLBACK(add_destroy_cb), this);
  gtk_widget_show (add->root());
}
void
ScoreView :: add_clicked_cb (GtkWidget * button, gpointer view_gpointer)
{
  static_cast<ScoreView*>(view_gpointer)->on_add ();
}

void
ScoreView :: on_remove ()
{
  GtkTreeSelection * sel (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view)));
  GtkTreeModel * model;
  GtkTreeIter iter;
  if (gtk_tree_selection_get_selected (sel, &model, &iter))
  {
    int index (-1);
    gtk_tree_model_get (model, &iter, COLUMN_DATA, &index, -1);
    const Scorefile::Item& item (_items[index]);
    _data.comment_out_scorefile_line (item.filename, item.begin_line, item.end_line, true);
    tree_view_refresh ();
  }
}

void
ScoreView :: remove_clicked_cb (GtkWidget * button, gpointer view_gpointer)
{
  static_cast<ScoreView*>(view_gpointer)->on_remove ();
}

void
ScoreView :: tree_view_refresh ()
{
  _items.clear ();
  _data.get_article_scores (_group, _article, _items);

  //GtkTreeSelection * selection (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view)));
  //bool found_selected (false);
  //GtkTreeIter selected_iter;
  //ScoreViewItem * selected_item = get_selected_item (d);

  gtk_list_store_clear (_store);

  int score (0);
  for (size_t i(0), len(_items.size()); i<len; ++i)
  {
    GtkTreeIter iter;
    const Scorefile::Item& item (_items[i]);

    // build the delta column
    char delta_str[32];
    if (item.value_assign_flag)
      g_snprintf (delta_str, sizeof(delta_str), "=%d", item.value);
    else
      g_snprintf (delta_str, sizeof(delta_str), "%c%d", (item.value<0?'-':'+'), abs(item.value));

    // build the value column
    char value_str[32];
    if (item.value_assign_flag)
      score = item.value;
    else
      score += item.value;
    g_snprintf (value_str, sizeof(value_str), "%d", score);

    // build the criteria column: file & line numbers, optional name, criteria
    GString * criteria = g_string_new (0);
    g_string_printf (criteria, _("File %s, Lines %d - %d"), item.filename.c_str(), (int)item.begin_line, (int)item.end_line);
    g_string_append_c (criteria, '\n');
    if (!item.name.empty()) {
      g_string_append_printf (criteria, _("Name: \"%s\""), item.name.c_str());
      g_string_append_c (criteria, '\n');
    }
    g_string_append (criteria, item.describe().c_str());
    if (criteria->str[criteria->len-1] == '\n')
      g_string_erase (criteria, criteria->len-1, 1);

    // add the new row to the model
    gtk_list_store_append (_store, &iter);
    gtk_list_store_set (_store, &iter, COLUMN_SCORE_DELTA, delta_str,
                                       COLUMN_SCORE_VALUE, value_str,
                                       COLUMN_CRITERIA, criteria->str,
                                       COLUMN_DATA, i, // index into _items
                                       -1);

    // cleanup 
    g_string_free (criteria, TRUE);
  }
}

ScoreView :: ScoreView (Data& data, GtkWindow* parent,
                        const Quark& group, const Article& article):
  _data (data),
  _group (group),
  _article (article),
  _root (0)
{
  GtkWidget * w = _root = gtk_dialog_new_with_buttons (_("Pan: Article's Scores"),
	                                               parent,
	                                               GTK_DIALOG_DESTROY_WITH_PARENT,
	                                               GTK_STOCK_CLOSE, GTK_RESPONSE_OK,
	                                               NULL);
  GtkTooltips * tips = gtk_tooltips_new ();
  g_object_ref_sink_pan (G_OBJECT(tips));
  g_object_weak_ref (G_OBJECT(w), (GWeakNotify)g_object_unref, tips);

  GtkWidget * button = create_rescore_button ();
  gtk_widget_show (button); 
  gtk_dialog_add_action_widget (GTK_DIALOG(w), button, GTK_RESPONSE_APPLY);
  gtk_window_set_policy (GTK_WINDOW(w), true, true, true);
  g_signal_connect (GTK_OBJECT(w), "response", G_CALLBACK(response_cb), &data);
  g_object_set_data_full (G_OBJECT(w), "score_view", this, delete_score_view);

  // workarea
  GtkWidget * hbox = gtk_hbox_new (FALSE, PAD);
  gtk_container_set_border_width (GTK_CONTAINER(hbox), 12);
  gtk_box_pack_start (GTK_BOX(GTK_DIALOG(w)->vbox), hbox, TRUE, TRUE, 0);
  gtk_widget_show (hbox);

  // create the list store & view
  _store = gtk_list_store_new (COLUMN_QTY, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT);
  tree_view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (_store));
  gtk_tree_view_set_rules_hint (GTK_TREE_VIEW(tree_view), true);
  gtk_widget_show (tree_view);

  // add COLUMN_SCORE_DELTA
  GtkCellRenderer * renderer = gtk_cell_renderer_text_new ();
  GtkTreeViewColumn * column = gtk_tree_view_column_new_with_attributes (_("Add"), renderer, "text", COLUMN_SCORE_DELTA, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(tree_view), column);

  // add COLUMN_SCORE_VALUE
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("New Score"), renderer, "text", COLUMN_SCORE_VALUE, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(tree_view), column);

  // add COLUMN_CRITERIA
  renderer = gtk_cell_renderer_text_new ();
  column = gtk_tree_view_column_new_with_attributes (_("Criteria"), renderer, "text", COLUMN_CRITERIA, NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW(tree_view), column);

  // set the selection mode
  GtkTreeSelection * selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view));
  gtk_tree_selection_set_mode (selection, GTK_SELECTION_SINGLE);

  w = gtk_scrolled_window_new (NULL, NULL);
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(w), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW(w), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER(w), tree_view);
  gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, TRUE, 0);
  gtk_widget_set_size_request (w, 500, 300);
  gtk_widget_show (w);

  // button box
  GtkWidget * bbox = gtk_vbox_new (FALSE, PAD_SMALL);
  gtk_box_pack_start (GTK_BOX (hbox), bbox, FALSE, FALSE, 0);
  gtk_widget_show (bbox);

  // add button
  w = gtk_button_new_from_stock (GTK_STOCK_ADD);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_tooltips_set_tip (tips, w, _("Add a New Scoring Rule"), NULL);
  gtk_widget_show (w);
  g_signal_connect (w, "clicked", G_CALLBACK(this->add_clicked_cb), this);

  // remove button
  w = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
  gtk_box_pack_start (GTK_BOX (bbox), w, FALSE, FALSE, 0);
  gtk_tooltips_set_tip (tips, w, _("Remove the Selected Scoring Rule"), NULL);
  gtk_widget_show (w);
  g_signal_connect (w, "clicked", G_CALLBACK(this->remove_clicked_cb), this);


  tree_view_refresh ();
  //button_refresh (d);
}
