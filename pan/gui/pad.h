#ifndef _PAD_H_
#define _PAD_H_

namespace pan
{
  extern void g_object_ref_sink_pan (GObject*);

  extern void pan_widget_set_tooltip_text (GtkWidget * w, const char * tip);

  extern void pan_box_pack_start_defaults (GtkBox * box, GtkWidget * child);
};

#define PAD_SMALL 3
#define PAD 6
#define PAD 6
#define PAD_BIG 9
#define PAD_LARGE 9

#endif
