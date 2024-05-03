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
#include <cassert>
#include <climits>
#include <glib/gi18n.h>
#include <pan/general/debug.h>
#include <pan/general/text-match.h>
#include "hig.h"
#include "pad.h"
#include "score-add-ui.h"
#include "score-view-ui.h"
#include <gtk/gtk.h>
#include <gdk/gdk.h>


using namespace pan;

/**
*** Key - Value paired comboboxes
**/
namespace
{
  enum { VALUE_STR, VALUE_TYPE, VALUE_COLS };

  GtkWidget * value_combo_new (GtkTreeModel * model=nullptr)
  {
    GtkWidget *  w = model
      ? gtk_combo_box_new_with_model (model)
      : gtk_combo_box_new ();
    GtkCellRenderer * renderer (gtk_cell_renderer_text_new ());
    gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (w), renderer, TRUE);
    gtk_cell_layout_set_attributes (GTK_CELL_LAYOUT (w), renderer, "text", VALUE_STR, nullptr);
    if (model)
      gtk_combo_box_set_active (GTK_COMBO_BOX(w), 0);
    return w;
  }

  int value_combo_get (GtkComboBox * w)
  {
    int type;
    GtkTreeIter iter;
    gtk_combo_box_get_active_iter (w, &iter);
    GtkTreeModel * model (gtk_combo_box_get_model (w));
    gtk_tree_model_get (model, &iter, VALUE_TYPE, &type, -1);
    return type;
  }

  void value_combo_set (GtkComboBox * w, int type)
  {
    GtkTreeIter iter;
    GtkTreeModel * model (gtk_combo_box_get_model (w));
    if (gtk_tree_model_get_iter_first (model, &iter)) do {
      int it_type;
      gtk_tree_model_get (model, &iter, VALUE_TYPE, &it_type, -1);
      if (type == it_type) {
        gtk_combo_box_set_active_iter (w, &iter);
        return;
      }
    } while (gtk_tree_model_iter_next (model, &iter));
  }

  /**
  ***
  **/

  enum { VAL_GT, VAL_LE };

  GtkTreeModel* gtle_tree_model_new ()
  {
    struct { int type; const char * str; } items[] = {
      { VAL_GT,  N_("is more than") },
      { VAL_LE,  N_("is at most") }
    };
    GtkListStore * store = gtk_list_store_new (VALUE_COLS, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, VALUE_TYPE, items[i].type,
                                        VALUE_STR, _(items[i].str),
                                        -1);
    }
    return GTK_TREE_MODEL(store);
  }

  /**
  ***
  **/

  enum { _ADD, _SUBTRACT, _ASSIGN, _WATCH, _IGNORE };

  GtkTreeModel * score_tree_model_new ()
  {
    struct { int type; const char * str; } items[] = {
      { _ADD,      N_("increase the article's score by") },
      { _SUBTRACT, N_("decrease the article's score by") },
      { _ASSIGN,   N_("set the article's score to") },
      { _WATCH,    N_("watch the article (set its score to 9999)") },
      { _IGNORE,   N_("ignore the article (set its score to -9999)") }
    };

    GtkListStore * store = gtk_list_store_new (VALUE_COLS, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, VALUE_TYPE, items[i].type,
                                        VALUE_STR, _(items[i].str),
                                        -1);
    }
    return GTK_TREE_MODEL(store);
  }

  /**
  ***
  **/

  enum Field { SUBJECT, AUTHOR, REFERENCES, LINES, BYTES, CROSSPOSTS, AGE };

  GtkTreeModel* field_tree_model_new ()
  {
    struct { int type; const char * str; } items[] = {
      { SUBJECT,    N_("Subject") },
      { AUTHOR,     N_("Author") },
      { REFERENCES, N_("References") },
      { LINES,      N_("Line Count") },
      { BYTES,      N_("Byte Count") },
      { CROSSPOSTS, N_("Crosspost Group Count") },
      { AGE,        N_("Age (in days)") }
    };

    GtkListStore * store = gtk_list_store_new (VALUE_COLS, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, VALUE_TYPE, items[i].type,
                                        VALUE_STR, _(items[i].str),
                                        -1);
    }
    return GTK_TREE_MODEL(store);
  }

  /**
  **/

  enum Days { MONTH=31, MONTHS=(31*6), FOREVER=0 };

  GtkTreeModel* time_tree_model_new ()
  {
    struct { int type; const char * str; } items[] = {
      { MONTH,      N_("for the next month") },
      { MONTHS,     N_("for the next six months") },
      { FOREVER,    N_("forever") },
    };

    GtkListStore * store = gtk_list_store_new (VALUE_COLS, G_TYPE_STRING, G_TYPE_INT);
    for (unsigned int i(0); i<G_N_ELEMENTS(items); ++i) {
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, VALUE_TYPE, items[i].type,
                                        VALUE_STR, _(items[i].str),
                                        -1);
    }
    return GTK_TREE_MODEL(store);
  }
}

/**
***  Text Match Combobox
**/
namespace
{
  enum { TEXT_MATCH_NAME, TEXT_MATCH_TYPE, TEXT_MATCH_NEGATE, TEXT_MATCH_COLS };

  GtkTreeModel * text_tree_model_new (bool allow_regex=true)
  {
    struct { int type; bool negate; const char * str; } items[] = {
      { TextMatch::CONTAINS,    false, N_("contains")},
      { TextMatch::CONTAINS,    true,  N_("doesn't contain")},
      { TextMatch::IS,          false, N_("is")},
      { TextMatch::IS,          true,  N_("isn't")},
      { TextMatch::BEGINS_WITH, false, N_("starts with")},
      { TextMatch::ENDS_WITH,   false, N_("ends with")},
      { TextMatch::REGEX,       false, N_("matches regex")},
    };

    GtkListStore * store = gtk_list_store_new (TEXT_MATCH_COLS, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
    for (unsigned int i(0); i<G_N_ELEMENTS(items); ++i) {
      if (!allow_regex && items[i].type==TextMatch::REGEX)
        continue;
      GtkTreeIter iter;
      gtk_list_store_append (store,  &iter);
      gtk_list_store_set (store, &iter, TEXT_MATCH_NAME, _(items[i].str),
                                        TEXT_MATCH_TYPE, items[i].type,
                                        TEXT_MATCH_NEGATE, items[i].negate,
                                        -1);
    }

    return GTK_TREE_MODEL(store);
  }

  void text_combo_set (GtkComboBox * w, int type, bool negate)
  {
    GtkTreeIter iter;
    GtkTreeModel * model (gtk_combo_box_get_model (w));
    if (gtk_tree_model_get_iter_first (model, &iter)) do {
      int it_type;
      gboolean it_negate;
      gtk_tree_model_get (model, &iter, TEXT_MATCH_TYPE, &it_type, TEXT_MATCH_NEGATE, &it_negate, -1);
      if (type==it_type && negate==it_negate) {
        gtk_combo_box_set_active_iter (w, &iter);
        return;
      }
    } while (gtk_tree_model_iter_next (model, &iter));
  }

  void text_combo_get (GtkWidget * w, int &type, bool &negate)
  {
    GtkTreeIter iter;
    if (gtk_combo_box_get_active_iter (GTK_COMBO_BOX(w), &iter)) {
      gboolean b;
      GtkTreeModel * model (gtk_combo_box_get_model (GTK_COMBO_BOX(w)));
      gtk_tree_model_get (model, &iter, TEXT_MATCH_TYPE, &type, TEXT_MATCH_NEGATE, &b, -1);
      negate = b;
    }
  }
}

/**
***  A Criteria Line
**/
namespace
{
  void field_changed_cb (GtkComboBox* w, gpointer h)
  {
    // get the children...
    GtkComboBox * criteria (GTK_COMBO_BOX (g_object_get_data (G_OBJECT(h), "criteria")));
    GtkWidget * entry      (GTK_WIDGET (g_object_get_data (G_OBJECT(h), "entry")));
    GtkWidget * spin       (GTK_WIDGET (g_object_get_data (G_OBJECT(h), "spin")));
    const Article * a      (static_cast<const Article*>(g_object_get_data (G_OBJECT(h), "article")));

    // get the state...
    const Field value ((Field)value_combo_get (w));

    // update the text entry field...
    std::string text;
    if (value == REFERENCES) text = a->message_id.to_string();
    else if (value == AUTHOR) text = a->author.to_string();
    else text = a->subject.to_string();
    gtk_entry_set_text (GTK_ENTRY(entry), text.c_str());

    // update the widgetry
    switch (value) {
      case SUBJECT:
      case AUTHOR:
      case REFERENCES: {
        GtkTreeModel * model = text_tree_model_new ();
        gtk_combo_box_set_model (criteria, model);
        text_combo_set (criteria,
                        value==REFERENCES ? TextMatch::CONTAINS : TextMatch::IS,
                        false);
        g_object_unref (model);
        // FIXME: need to update the entry here to subject / author / references
        gtk_widget_hide (spin);
        gtk_widget_show (entry);
        break;
      }

      case LINES:
      case BYTES:
      case AGE: {
        GtkTreeModel * model = gtle_tree_model_new ();
        gtk_combo_box_set_model (criteria, model);
        value_combo_set (criteria, (value==AGE ? VAL_LE : VAL_GT));
        g_object_unref (model);
        gtk_widget_hide (entry);
        gtk_widget_show (spin);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(spin), (value==AGE ? 7 : (value==LINES ? 100 : 1000)));
        break;
      }

      case CROSSPOSTS: {
        GtkTreeModel * model = gtle_tree_model_new ();
        gtk_combo_box_set_model (criteria, model);
        value_combo_set (criteria, VAL_GT);
        g_object_unref (model);
        gtk_widget_hide (entry);
        gtk_widget_show (spin);
        gtk_spin_button_set_value (GTK_SPIN_BUTTON(spin), 2);
        break;
      }
    }
  }

  GtkWidget* criteria_line_new (GtkWidget     *& setme_field,
                                GtkWidget     *& setme_criteria,
                                GtkWidget     *& setme_entry,
                                GtkWidget     *& setme_spin,
                                const Article  * article)
  {
    GtkWidget * h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
    g_object_set_data (G_OBJECT(h), "article", gpointer(article));

    GtkTreeModel * model = field_tree_model_new ();
    GtkWidget * field = setme_field = value_combo_new (model);
    g_object_unref (G_OBJECT(model));
    gtk_box_pack_start (GTK_BOX(h), field, false, false, 0);
    g_signal_connect (field, "changed", G_CALLBACK(field_changed_cb), h);
    g_object_set_data (G_OBJECT(h), "field", field);

    GtkWidget * criteria = setme_criteria = value_combo_new ();
    gtk_box_pack_start (GTK_BOX(h), criteria, false, false, 0);
    g_object_set_data (G_OBJECT(h), "criteria", criteria);

    GtkWidget * entry = setme_entry = gtk_entry_new ();
    gtk_entry_set_width_chars (GTK_ENTRY(entry), 40);
    gtk_box_pack_start (GTK_BOX(h), entry, true, true, 0);
    g_object_set_data (G_OBJECT(h), "entry", entry);

    GtkAdjustment * a = (GtkAdjustment*)gtk_adjustment_new (100, -ULONG_MAX, ULONG_MAX, 1.0, 1.0, 0.0);
    GtkWidget * spin = setme_spin = gtk_spin_button_new (a, 100.0, 0u);
    gtk_box_pack_start (GTK_BOX(h), spin, false, false, 0);
    g_object_set_data (G_OBJECT(h), "spin", spin);

    gtk_widget_show (field);
    gtk_widget_show (criteria);
    gtk_widget_show (h);
    field_changed_cb (GTK_COMBO_BOX(field), h);
    return h;
  }
}

/***
****
***/

void
ScoreAddDialog :: add_this_to_scorefile (bool do_rescore)
{

  // section
  bool negate (false);
  int value (0);
  std::string section_wildmat (gtk_entry_get_text (GTK_ENTRY(_section_entry)));
  text_combo_get (_section_menu, value, negate);
  switch (value) {
    case TextMatch::CONTAINS:     section_wildmat = "*" + section_wildmat + "*"; break;
    case TextMatch::BEGINS_WITH:  section_wildmat =       section_wildmat + "*"; break;
    case TextMatch::ENDS_WITH:    section_wildmat = "*" + section_wildmat;       break;
    case TextMatch::IS:           break;
  }
  if (negate)
    section_wildmat = "~" + section_wildmat;


  // criteria
  Scorefile::AddItem item;
  item.on = true;
  const int field (value_combo_get (GTK_COMBO_BOX(_field_menu)));
  switch (field) {
    case SUBJECT:    item.key = "Subject";    break;
    case AUTHOR:     item.key = "From";       break;
    case REFERENCES: item.key = "References"; break;
    case LINES:      item.key = "Lines";      break;
    case BYTES:      item.key = "Bytes";      break;
    case CROSSPOSTS: item.key = "Xref";       break;
    case AGE:        item.key = "Age";        break;
    default: assert(0);
  }
  if (field==SUBJECT || field==AUTHOR || field==REFERENCES)
  {
    text_combo_get (_criteria_menu, value, item.negate);
    const char * pch = gtk_entry_get_text (GTK_ENTRY(_text_criteria_entry));
    item.value = TextMatch::create_regex (pch, (TextMatch::Type)value);
  }
  else
  {
    const int rel = value_combo_get(GTK_COMBO_BOX(_criteria_menu));
    if (field == AGE)
    {
      // Note the reversed logic here:
      // slrn's scorefile matches "Age: 7" for articles <= 7 days old.
      int i = (int) gtk_spin_button_get_value (GTK_SPIN_BUTTON (_numeric_criteria_spin));
      char buf[2048];
      g_snprintf (buf, sizeof(buf), "%d", i);
      item.value = buf;
      item.negate = rel != VAL_LE;
    }
    else if (field == CROSSPOSTS)
    {
      // Xref is of form server group1:number1 group2:number2
      // so to count the crossposts, test for some number of colons.
      const int gt = (int) gtk_spin_button_get_value (GTK_SPIN_BUTTON (_numeric_criteria_spin));
      char buf[512];
      snprintf (buf, sizeof(buf), "(.*:){%d} %% crossposted to %d or more groups ", gt+1, gt+1);
      item.value = buf;
      item.negate = rel != VAL_GT;
    }
    else // lines, bytes
    {
      // slrn's scoreifle matches "Lines: 7" for articles with > 7 lines.
      // bytes works the same way.
      const unsigned long l = (unsigned long) gtk_spin_button_get_value (GTK_SPIN_BUTTON (_numeric_criteria_spin));
      char buf[2048];
      g_snprintf (buf, sizeof(buf), "%lu", l);
      item.value = buf;
      item.negate = rel != VAL_GT;
    }
  }

  // score
  int score (0);
  bool assign_flag (false);
  const int spin_score (gtk_spin_button_get_value_as_int (GTK_SPIN_BUTTON(_score_spin)));
  value = value_combo_get (GTK_COMBO_BOX(_score_menu));
  switch (value) {
    case _ADD:       assign_flag=false; score =  spin_score; break;
    case _SUBTRACT:  assign_flag=false; score = -spin_score; break;
    case _ASSIGN:    assign_flag=true;  score =  spin_score; break;
    case _WATCH:     assign_flag=true;  score =  9999;       break;
    case _IGNORE:    assign_flag=true;  score = -9999;       break;
  }

  // duration in days
  const int days (value_combo_get (GTK_COMBO_BOX(_duration_menu)));

  // save it
  _data.add_score (section_wildmat,
                   score, assign_flag,
                   days,
                   false, &item, 1, do_rescore);
}

void
ScoreAddDialog :: response_cb (GtkDialog * w, int response, gpointer dialog_gpointer)
{
  bool add (false);
  bool close (false);
  ScoreAddDialog * self (static_cast<ScoreAddDialog*>(dialog_gpointer));

  if (response == GTK_RESPONSE_CANCEL)
  {
    close = true;
  }
  else if ((response == GTK_RESPONSE_OK || response == GTK_RESPONSE_APPLY))
  {
    // are there any assignment items on this article already?
    bool assign (false);
    Scorefile::items_t items;
    self->_data.get_article_scores (self->_group, self->_article, items);
    for (Scorefile::items_t::const_iterator it(items.begin()), end(items.end()); !assign && it!=end; ++it)
      assign |= it->value_assign_flag;

    if (!assign) // no conflict...
    {
      add = close = true;
    }
    else
    {
      GtkWidget * d (gtk_message_dialog_new_with_markup (
        GTK_WINDOW(self->_root),
        GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE, nullptr));
      HIG :: message_dialog_set_text (GTK_MESSAGE_DIALOG(d),
        _("Another rule already sets this article's score."),
        _("You may want to go back or delete the old rule."));
      gtk_dialog_add_buttons (GTK_DIALOG(d),
                              _("Back"), -20,
                              _("Delete"), -21,
                              _("Add"), -22,
        nullptr);
      const int conflict_response (gtk_dialog_run (GTK_DIALOG(d)));
      gtk_widget_destroy (d);
      if (conflict_response == -20) // go back
        return;
      else if (conflict_response == -22) // add
        add = close = true;
      else { // delete
        ScoreView * score_view = new ScoreView (self->_data, GTK_WINDOW(w), self->_group, self->_article);
        gtk_widget_show_all (score_view->root());
      }
    }
  }

  if (add)
    self->add_this_to_scorefile (response==GTK_RESPONSE_APPLY);
  if (close)
    gtk_widget_destroy (GTK_WIDGET(w));
}

void
ScoreAddDialog :: populate (const Quark& group, const Article& a, Mode mode)
{
  // section
  text_combo_set (GTK_COMBO_BOX(_section_menu), TextMatch::IS, false);
  const char * g = group.empty() ? "" : group.c_str();
  gtk_entry_set_text (GTK_ENTRY(_section_entry), g);

  // criteria
  int field;
  switch (mode) {
    case PLONK: field = AUTHOR; break;
    case WATCH_SUBTHREAD:
    case IGNORE_SUBTHREAD: field = REFERENCES; break;
    default: field = SUBJECT; break;
  }
  value_combo_set (GTK_COMBO_BOX(_field_menu), field);
  text_combo_set (GTK_COMBO_BOX(_criteria_menu),
                     field==REFERENCES ? TextMatch::CONTAINS : TextMatch::IS,
                     false);
  std::string text;
  if (field == REFERENCES) text = a.message_id.to_string();
  else if (field == AUTHOR) text = a.author.to_string();
  else text = a.subject.to_string();
  gtk_entry_set_text (GTK_ENTRY(_text_criteria_entry), text.c_str());

  // score
  int score_mode;
  switch (mode) {
    case WATCH_SUBTHREAD: score_mode = _WATCH; break;
    case PLONK:
    case IGNORE_SUBTHREAD: score_mode = _IGNORE; break;
    default: score_mode = _ADD; break;
  }
  gtk_spin_button_set_value (GTK_SPIN_BUTTON(_score_spin), 100);
  value_combo_set (GTK_COMBO_BOX(_score_menu), score_mode);

  // duration
  value_combo_set (GTK_COMBO_BOX(_duration_menu), MONTH);
}

namespace
{
  GtkWidget* create_rescore_button ()
  {
    GtkWidget * button = gtk_button_new ();
    GtkWidget * label = gtk_label_new_with_mnemonic (_("Add and Re_score"));
    gtk_label_set_mnemonic_widget (GTK_LABEL (label), GTK_WIDGET (button));

    GtkWidget * image = gtk_image_new_from_icon_name ("list-add", GTK_ICON_SIZE_BUTTON);
    GtkWidget * image2 = gtk_image_new_from_icon_name ("view-refresh", GTK_ICON_SIZE_BUTTON);
    GtkWidget * hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 2);

    gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (hbox), image2, FALSE, FALSE, 0);
    gtk_box_pack_end (GTK_BOX (hbox), label, FALSE, FALSE, 0);

    gtk_container_add (GTK_CONTAINER (button), hbox);
    gtk_widget_show_all (hbox);

    return button;
  }

  void delete_score_add_dialog (gpointer dialog_gpointer)
  {
    delete static_cast<ScoreAddDialog*>(dialog_gpointer);
  }
}

namespace
{
  void score_combo_changed_cb (GtkComboBox* w, gpointer spin_gpointer)
  {
    const int value = value_combo_get (w);

    GtkWidget * spin (GTK_WIDGET (spin_gpointer));
    if (value==_WATCH || value==_IGNORE)
      gtk_widget_hide (spin);
    else
      gtk_widget_show (spin);
  }
}

ScoreAddDialog :: ScoreAddDialog (Data           & data,
                                  GtkWidget      * parent,
                                  const Quark    & group,
                                  const Article  & article,
                                  Mode             mode):
  _data (data),
  _article (article),
  _group (group),
  _root (nullptr)
{
  std::string s (_("Pan"));
  s += ": ";
  s += _("New Scoring Rule");
  GtkWidget * w = _root = gtk_dialog_new_with_buttons (s.c_str(),
    GTK_WINDOW(gtk_widget_get_toplevel(parent)),
    GTK_DIALOG_DESTROY_WITH_PARENT,
    _("Cancel"), GTK_RESPONSE_CANCEL,
    _("Add"), GTK_RESPONSE_OK,
    nullptr);
  g_object_set_data_full (G_OBJECT(w), "dialog", this, delete_score_add_dialog);
  GtkWidget * button = create_rescore_button ();
  gtk_widget_show (button);
  gtk_dialog_add_action_widget (GTK_DIALOG(w), button, GTK_RESPONSE_APPLY);
  g_signal_connect (w, "response", G_CALLBACK(response_cb), this);

  /**
  ***  workarea
  **/

  int row = 0;
  GtkWidget * t = HIG :: workarea_create ();
  gtk_box_pack_start (GTK_BOX( gtk_dialog_get_content_area( GTK_DIALOG(_root))), t, true, true, 0);
  HIG::workarea_add_section_title (t, &row, _("New Scoring Rule"));
  HIG::workarea_add_section_spacer (t, row, 4);

  // section
  GtkWidget * h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD);
  GtkTreeModel * model = text_tree_model_new (false);
  w = _section_menu = value_combo_new (model);
  g_object_unref (G_OBJECT(model));
  gtk_box_pack_start (GTK_BOX(h), w, false, false, 0);
  w = _section_entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX(h), w, true, true, 0);
  HIG::workarea_add_row (t, &row, _("Group name"), h);
  gtk_widget_show_all (h);

  // criteria
  w = criteria_line_new (_field_menu, _criteria_menu,
                         _text_criteria_entry, _numeric_criteria_spin,
                         &_article);
  HIG::workarea_add_row (t, &row, _("and"), w);
  gtk_widget_show (w);

  // score
  h = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, PAD_SMALL);
  model = score_tree_model_new ();
  w = _score_menu = value_combo_new (model);
  g_object_unref (model);
  gtk_box_pack_start (GTK_BOX(h), w, true, true, 0);
  GtkAdjustment * a = (GtkAdjustment*)gtk_adjustment_new (100, INT_MIN, INT_MAX, 1.0, 1.0, 0.0);
  w = _score_spin = gtk_spin_button_new (a, 100.0, 0u);
  gtk_box_pack_start (GTK_BOX(h), w, true, true, 0);
  HIG::workarea_add_wide_control (t, &row, h);
  gtk_widget_show_all (h);
  g_signal_connect (_score_menu, "changed", G_CALLBACK(score_combo_changed_cb), w);

  // duration
  model = time_tree_model_new ();
  w = _duration_menu = value_combo_new (model);
  g_object_unref (model);
  HIG::workarea_add_wide_control (t, &row, w);
  gtk_widget_show (w);

  populate (group, article, mode);
  gtk_widget_show (t);
}
