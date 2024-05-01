#ifndef __SCORE_ADD_UI_H__
#define __SCORE_ADD_UI_H__

#include <gtk/gtk.h>
#include <gdk/gdk.h>
#include <pan/data/article.h>
#include <pan/data/data.h>

namespace pan
{
  /**
   * Dialog for creating a new entry for the Scorefile.
   * @ingroup GUI
   */
  struct ScoreAddDialog
  {
    public:
      enum Mode { ADD, PLONK, WATCH_SUBTHREAD, IGNORE_SUBTHREAD };
      ScoreAddDialog (Data& data, GtkWidget* parent, const Quark& group, const Article&, Mode);

    public:
      GtkWidget* root() { return _root; }

    private:
      void populate (const Quark& group, const Article&, Mode);
      void add_this_to_scorefile (bool do_rescore_all);
      static void response_cb (GtkDialog*, int response, gpointer);

    private:
      Data& _data;
      const Article _article;
      Quark _group;
      GtkWidget * _root;
      GtkWidget * _section_menu;
      GtkWidget * _section_entry;
      GtkWidget * _field_menu;
      GtkWidget * _criteria_menu;
      GtkWidget * _text_criteria_entry;
      GtkWidget * _numeric_criteria_spin;
      GtkWidget * _score_menu;
      GtkWidget * _score_spin;
      GtkWidget * _duration_menu;
  };
}

#endif
