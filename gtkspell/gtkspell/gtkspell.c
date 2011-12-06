/* gtkspell - a spell-checking addon for GTK's TextView widget
 * Copyright (c) 2002 Evan Martin.
 */

/* vim: set ts=4 sw=4 wm=5 : */

#include <string.h>
#include <gtk/gtk.h>
#include <libintl.h>
#include <locale.h>
#include "../config.h"
#include "gtkspell.h"

#define _(String) dgettext (PACKAGE, String)

#define GTKSPELL_MISSPELLED_TAG "gtkspell-misspelled"

#include <enchant.h>

static const int debug = 0;
static const int quiet = 0;

static EnchantBroker *broker = NULL;
static int broker_ref_cnt;

struct _GtkSpell {
	GtkTextView *view;
	GtkTextBuffer *buffer;
	GtkTextTag *tag_highlight;
	GtkTextMark *mark_insert_start;
	GtkTextMark *mark_insert_end;
	gboolean deferred_check;
	EnchantDict *speller;
	GtkTextMark *mark_click;
	gchar *lang;
};

static void gtkspell_free(GtkSpell *spell);

#define GTKSPELL_OBJECT_KEY "gtkspell"

GQuark
gtkspell_error_quark(void) {
	static GQuark q = 0;
	if (q == 0)
		q = g_quark_from_static_string("gtkspell-error-quark");
	return q;
}

static gboolean
gtkspell_text_iter_forward_word_end(GtkTextIter *i) {
	GtkTextIter iter;

/* heuristic: 
 * if we're on an singlequote/apostrophe and
 * if the next letter is alphanumeric,
 * this is an apostrophe. */

	if (!gtk_text_iter_forward_word_end(i))
		return FALSE;

	if (gtk_text_iter_get_char(i) != '\'')
		return TRUE;

	iter = *i;
	if (gtk_text_iter_forward_char(&iter)) {
		if (g_unichar_isalpha(gtk_text_iter_get_char(&iter))) {
			return (gtk_text_iter_forward_word_end(i));
		}
	}

	return TRUE;
}

static gboolean
gtkspell_text_iter_backward_word_start(GtkTextIter *i) {
	GtkTextIter iter;

	if (!gtk_text_iter_backward_word_start(i))
		return FALSE;

	iter = *i;
	if (gtk_text_iter_backward_char(&iter)) {
		if (gtk_text_iter_get_char(&iter) == '\'') {
			if (gtk_text_iter_backward_char(&iter)) {
				if (g_unichar_isalpha(gtk_text_iter_get_char(&iter))) {
					return (gtk_text_iter_backward_word_start(i));
				}
			}
		}
	}

	return TRUE;
}

#define gtk_text_iter_backward_word_start gtkspell_text_iter_backward_word_start
#define gtk_text_iter_forward_word_end gtkspell_text_iter_forward_word_end

static void
check_word(GtkSpell *spell, GtkTextBuffer *buffer,
           GtkTextIter *start, GtkTextIter *end) {
	char *text;
	if (!spell->speller)
		return;
	text = gtk_text_buffer_get_text(buffer, start, end, FALSE);
	if (debug) g_print("checking: %s\n", text);
	if (g_unichar_isdigit(*text) == FALSE) /* don't check numbers */
		if (enchant_dict_check(spell->speller, text, strlen(text)) != 0)
			gtk_text_buffer_apply_tag(buffer, spell->tag_highlight, start, end);
	g_free(text);
}

static void
print_iter(char *name, GtkTextIter *iter) {
	g_print("%1s[%d%c%c%c] ", name, gtk_text_iter_get_offset(iter),
		gtk_text_iter_starts_word(iter) ? 's' : ' ',
		gtk_text_iter_inside_word(iter) ? 'i' : ' ',
		gtk_text_iter_ends_word(iter) ? 'e' : ' ');
}

static void
check_range(GtkSpell *spell, GtkTextBuffer *buffer,
            GtkTextIter start, GtkTextIter end, gboolean force_all) {
	/* we need to "split" on word boundaries.
	 * luckily, pango knows what "words" are 
	 * so we don't have to figure it out. */

	GtkTextIter wstart, wend, cursor, precursor;
	gboolean inword, highlight;
	if (debug) {
		g_print("check_range: "); print_iter("s", &start); print_iter("e", &end); g_print(" -> ");
	}

	if (gtk_text_iter_inside_word(&end))
		gtk_text_iter_forward_word_end(&end);
	if (!gtk_text_iter_starts_word(&start)) {
		if (gtk_text_iter_inside_word(&start) || 
				gtk_text_iter_ends_word(&start)) {
			gtk_text_iter_backward_word_start(&start);
		} else {
			/* if we're neither at the beginning nor inside a word,
			 * me must be in some spaces.
			 * skip forward to the beginning of the next word. */
			//gtk_text_buffer_remove_tag(buffer, tag_highlight, &start, &end);
			if (gtk_text_iter_forward_word_end(&start))
				gtk_text_iter_backward_word_start(&start);
		}
	}
	gtk_text_buffer_get_iter_at_mark(buffer, &cursor,
			gtk_text_buffer_get_insert(buffer));

	precursor = cursor;
	gtk_text_iter_backward_char(&precursor);
	highlight = gtk_text_iter_has_tag(&cursor, spell->tag_highlight) ||
			gtk_text_iter_has_tag(&precursor, spell->tag_highlight);
	
	gtk_text_buffer_remove_tag(buffer, spell->tag_highlight, &start, &end);

	/* Fix a corner case when replacement occurs at beginning of buffer:
	 * An iter at offset 0 seems to always be inside a word,
	 * even if it's not.  Possibly a pango bug.
	 */
	if (gtk_text_iter_get_offset(&start) == 0) {
		gtk_text_iter_forward_word_end(&start);
		gtk_text_iter_backward_word_start(&start);
	}

	if (debug) {print_iter("s", &start); print_iter("e", &end); g_print("\n");}

	wstart = start;
	while (gtk_text_iter_compare(&wstart, &end) < 0) {
		/* move wend to the end of the current word. */
		wend = wstart;
		gtk_text_iter_forward_word_end(&wend);

		inword = (gtk_text_iter_compare(&wstart, &cursor) < 0) && 
				(gtk_text_iter_compare(&cursor, &wend) <= 0);

		if (inword && !force_all) {
			/* this word is being actively edited, 
			 * only check if it's already highligted,
			 * otherwise defer this check until later. */
			if (highlight)
				check_word(spell, buffer, &wstart, &wend);
			else
				spell->deferred_check = TRUE;
		} else {
			check_word(spell, buffer, &wstart, &wend);
			spell->deferred_check = FALSE;
		}

		/* now move wend to the beginning of the next word, */
		gtk_text_iter_forward_word_end(&wend);
		gtk_text_iter_backward_word_start(&wend);
		/* make sure we've actually advanced
		 * (we don't advance in some corner cases), */
		if (gtk_text_iter_equal(&wstart, &wend))
			break; /* we're done in these cases.. */
		/* and then pick this as the new next word beginning. */
		wstart = wend;
	}
}

static void
check_deferred_range(GtkSpell *spell, GtkTextBuffer *buffer, gboolean force_all) {
	GtkTextIter start, end;
	gtk_text_buffer_get_iter_at_mark(buffer, &start, spell->mark_insert_start);
	gtk_text_buffer_get_iter_at_mark(buffer, &end, spell->mark_insert_end);
	check_range(spell, buffer, start, end, force_all);
}

/* insertion works like this:
 *  - before the text is inserted, we mark the position in the buffer.
 *  - after the text is inserted, we see where our mark is and use that and
 *    the current position to check the entire range of inserted text.
 *
 * this may be overkill for the common case (inserting one character). */

static void
insert_text_before(GtkTextBuffer *buffer, GtkTextIter *iter,
                   gchar *text, gint len, GtkSpell *spell) {
	gtk_text_buffer_move_mark(buffer, spell->mark_insert_start, iter);
}

static void
insert_text_after(GtkTextBuffer *buffer, GtkTextIter *iter,
                  gchar *text, gint len, GtkSpell *spell) {
	GtkTextIter start;

	if (debug) g_print("insert\n");

	/* we need to check a range of text. */
	gtk_text_buffer_get_iter_at_mark(buffer, &start, spell->mark_insert_start);
	check_range(spell, buffer, start, *iter, FALSE);
	
	gtk_text_buffer_move_mark(buffer, spell->mark_insert_end, iter);
}

/* deleting is more simple:  we're given the range of deleted text.
 * after deletion, the start and end iters should be at the same position
 * (because all of the text between them was deleted!).
 * this means we only really check the words immediately bounding the
 * deletion.
 */

static void
delete_range_after(GtkTextBuffer *buffer,
                   GtkTextIter *start, GtkTextIter *end, GtkSpell *spell) {
	if (debug) g_print("delete\n");
	check_range(spell, buffer, *start, *end, FALSE);
}

static void
mark_set(GtkTextBuffer *buffer, GtkTextIter *iter, 
		 GtkTextMark *mark, GtkSpell *spell) {
	/* if the cursor has moved and there is a deferred check so handle it now */
	if ((mark == gtk_text_buffer_get_insert(buffer)) && spell->deferred_check)
		check_deferred_range(spell, buffer, FALSE);
}

static void
get_word_extents_from_mark(GtkTextBuffer *buffer,
                     GtkTextIter *start, GtkTextIter *end, GtkTextMark *mark) {
	gtk_text_buffer_get_iter_at_mark(buffer, start, mark);
	if (!gtk_text_iter_starts_word(start)) 
		gtk_text_iter_backward_word_start(start);
	*end = *start;
	if (gtk_text_iter_inside_word(end))
		gtk_text_iter_forward_word_end(end);
}

static void
add_to_dictionary(GtkWidget *menuitem, GtkSpell *spell) {
	char *word;
	GtkTextIter start, end;

	get_word_extents_from_mark(spell->buffer, &start, &end, spell->mark_click);
	word = gtk_text_buffer_get_text(spell->buffer, &start, &end, FALSE);
	
	enchant_dict_add_to_pwl( spell->speller, word, strlen(word));

	gtkspell_recheck_all(spell);

	g_free(word);
}

static void
ignore_all(GtkWidget *menuitem, GtkSpell *spell) {
	char *word;
	GtkTextIter start, end;

	get_word_extents_from_mark(spell->buffer, &start, &end, spell->mark_click);
	word = gtk_text_buffer_get_text(spell->buffer, &start, &end, FALSE);
	
	enchant_dict_add_to_session(spell->speller, word, strlen(word));

	gtkspell_recheck_all(spell);

	g_free(word);
}

static void
replace_word(GtkWidget *menuitem, GtkSpell *spell) {
	char *oldword;
	const char *newword;
	GtkTextIter start, end;
	
	if (!spell->speller)
		return;

	get_word_extents_from_mark(spell->buffer, &start, &end, spell->mark_click);
	oldword = gtk_text_buffer_get_text(spell->buffer, &start, &end, FALSE);
	newword = gtk_label_get_text(GTK_LABEL
				     (gtk_bin_get_child(GTK_BIN(menuitem))));

	if (debug) {
		g_print("old word: '%s'\n", oldword);
		print_iter("s", &start); print_iter("e", &end);
		g_print("\nnew word: '%s'\n", newword);
	}

	gtk_text_buffer_begin_user_action(spell->buffer);
	gtk_text_buffer_delete(spell->buffer, &start, &end);
	gtk_text_buffer_insert(spell->buffer, &start, newword, -1);
	gtk_text_buffer_end_user_action(spell->buffer);

	enchant_dict_store_replacement(spell->speller, 
			oldword, strlen(oldword),
			newword, strlen(newword));

	g_free(oldword);
}

/* This function populates suggestions at the top of the passed menu */
static void
add_suggestion_menus(GtkSpell *spell, GtkTextBuffer *buffer,
                      const char *word, GtkWidget *topmenu) {
	GtkWidget *menu;
	GtkWidget *mi;
	char **suggestions;
	size_t n_suggs, i;
	char *label;
	
	menu = topmenu;

	if (!spell->speller)
		return;

	gint menu_position = 0;

	suggestions = enchant_dict_suggest(spell->speller, word, strlen(word), &n_suggs);

	if (suggestions == NULL || !n_suggs) {
		/* no suggestions.  put something in the menu anyway... */
		GtkWidget *label;
		label = gtk_label_new("");
		gtk_label_set_markup(GTK_LABEL(label), _("<i>(no suggestions)</i>"));

		mi = gtk_menu_item_new();
		gtk_container_add(GTK_CONTAINER(mi), label);
		gtk_widget_show_all(mi);
		gtk_menu_shell_insert(GTK_MENU_SHELL(menu), mi, menu_position++);
	} else {
		/* build a set of menus with suggestions. */
		gboolean inside_more_submenu = FALSE;
		for (i = 0; i < n_suggs; i++ ) {
			if (i > 0 && i % 10 == 0) {
				inside_more_submenu = TRUE;
				mi = gtk_menu_item_new_with_label(_("More..."));
				gtk_widget_show(mi);
				gtk_menu_shell_insert(GTK_MENU_SHELL(menu), mi, menu_position++);

				menu = gtk_menu_new();
				gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), menu);
			}
			mi = gtk_menu_item_new_with_label(suggestions[i]);
			g_signal_connect(G_OBJECT(mi), "activate",
					G_CALLBACK(replace_word), spell);
			gtk_widget_show(mi);
			if (inside_more_submenu) gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);
			else gtk_menu_shell_insert(GTK_MENU_SHELL(menu), mi, menu_position++);
		}
	}

	if (suggestions)
		enchant_dict_free_string_list(spell->speller, suggestions);

	/* + Add to Dictionary */
	label = g_strdup_printf(_("Add \"%s\" to Dictionary"), word);
	mi = gtk_image_menu_item_new_with_label(label);
	g_free(label);
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), 
			gtk_image_new_from_stock(GTK_STOCK_ADD, GTK_ICON_SIZE_MENU));
	g_signal_connect(G_OBJECT(mi), "activate",
			G_CALLBACK(add_to_dictionary), spell);
	gtk_widget_show_all(mi);
	gtk_menu_shell_insert(GTK_MENU_SHELL(topmenu), mi, menu_position++);

	/* - Ignore All */
	mi = gtk_image_menu_item_new_with_label(_("Ignore All"));
	gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(mi), 
			gtk_image_new_from_stock(GTK_STOCK_REMOVE, GTK_ICON_SIZE_MENU));
	g_signal_connect(G_OBJECT(mi), "activate",
			G_CALLBACK(ignore_all), spell);
	gtk_widget_show_all(mi);
	gtk_menu_shell_insert(GTK_MENU_SHELL(topmenu), mi, menu_position++);
}

static GtkWidget*
build_suggestion_menu(GtkSpell *spell, GtkTextBuffer *buffer,
                      const char *word) {
	GtkWidget *topmenu;
	topmenu = gtk_menu_new();
	add_suggestion_menus(spell, buffer, word, topmenu);

	return topmenu;
}

static void
language_change_callback(GtkCheckMenuItem *mi, GtkSpell* spell) {
	if (gtk_check_menu_item_get_active(mi)) {
		GError* error = NULL;
		gchar *name;
		g_object_get(G_OBJECT(mi), "name", &name, NULL);
		gtkspell_set_language(spell, name, &error);
		g_free(name);
	}
}

struct _languages_cb_struct {GList *langs;};

static void
dict_describe_cb(const char * const lang_tag,
		 const char * const provider_name,
		 const char * const provider_desc,
		 const char * const provider_file,
		 void * user_data) {

	struct _languages_cb_struct *languages_cb_struct = (struct _languages_cb_struct *)user_data;

	languages_cb_struct->langs = g_list_insert_sorted(
		languages_cb_struct->langs, g_strdup(lang_tag),
		(GCompareFunc) strcmp);
}

static GtkWidget*
build_languages_menu(GtkSpell *spell) {
	GtkWidget *active_item = NULL, *menu = gtk_menu_new();
	GList *langs;
	GSList *menu_group = NULL;

	struct _languages_cb_struct languages_cb_struct;
	languages_cb_struct.langs = NULL;

	enchant_broker_list_dicts(broker, dict_describe_cb, &languages_cb_struct);

	langs = languages_cb_struct.langs;

	for (; langs; langs = langs->next) {
		gchar *lang_tag = langs->data;
		GtkWidget* mi = gtk_radio_menu_item_new_with_label(menu_group, lang_tag);
		menu_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(mi));

		g_object_set(G_OBJECT(mi), "name", lang_tag, NULL);
		if (strcmp(spell->lang, lang_tag) == 0)
			active_item = mi;
		else
			g_signal_connect(G_OBJECT(mi), "activate",
				G_CALLBACK(language_change_callback), spell);
		gtk_widget_show(mi);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), mi);

		g_free(lang_tag);
	}
	if (active_item)
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(active_item), TRUE);

	g_list_free(languages_cb_struct.langs);

	return menu;
}

static void
populate_popup(GtkTextView *textview, GtkMenu *menu, GtkSpell *spell) {
	GtkWidget *mi;
	GtkTextIter start, end;
	char *word;

	/* menu separator comes first. */
	mi = gtk_separator_menu_item_new();
	gtk_widget_show(mi);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);

	/* on top: language selection */
	mi = gtk_menu_item_new_with_label(_("Languages"));
	gtk_menu_item_set_submenu(GTK_MENU_ITEM(mi), build_languages_menu(spell));
	gtk_widget_show_all(mi);
	gtk_menu_shell_prepend(GTK_MENU_SHELL(menu), mi);

	/* we need to figure out if they picked a misspelled word. */
	get_word_extents_from_mark(spell->buffer, &start, &end, spell->mark_click);

	/* if our highlight algorithm ever messes up, 
	 * this isn't correct, either. */
	if (!gtk_text_iter_has_tag(&start, spell->tag_highlight))
		return; /* word wasn't misspelled. */

	/* then, on top of it, the suggestions */
	word = gtk_text_buffer_get_text(spell->buffer, &start, &end, FALSE);
	add_suggestion_menus(spell, spell->buffer, word, GTK_WIDGET (menu) );
	g_free(word);
}

/* when the user right-clicks on a word, they want to check that word.
 * here, we do NOT  move the cursor to the location of the clicked-upon word
 * since that prevents the use of edit functions on the context menu. */
static gboolean
button_press_event(GtkTextView *view, GdkEventButton *event, GtkSpell *spell) {
	if (event->button == 3) {
		gint x, y;
		GtkTextIter iter;

		/* handle deferred check if it exists */
		if (spell->deferred_check)
			check_deferred_range(spell, spell->buffer, TRUE);

		gtk_text_view_window_to_buffer_coords(view, 
				GTK_TEXT_WINDOW_TEXT, 
				event->x, event->y,
				&x, &y);
		gtk_text_view_get_iter_at_location(view, &iter, x, y);
		gtk_text_buffer_move_mark(spell->buffer, spell->mark_click, &iter);
	}
	return FALSE; /* false: let gtk process this event, too.
					 we don't want to eat any events. */
}

/* This event occurs when the popup menu is requested through a key-binding
 * (Menu Key or <shift>+F10 by default).  In this case we want to set
 * spell->mark_click to the cursor position. */
static gboolean
popup_menu_event(GtkTextView *view, GtkSpell *spell) {
	GtkTextIter iter;

	gtk_text_buffer_get_iter_at_mark(spell->buffer, &iter, 
			gtk_text_buffer_get_insert(spell->buffer));
	gtk_text_buffer_move_mark(spell->buffer, spell->mark_click, &iter);
	return FALSE; /* false: let gtk process this event, too. */
}

static void
_set_lang_from_dict(const char * const lang_tag,
		    const char * const provider_name,
		    const char * const provider_desc,
		    const char * const provider_dll_file,
		    void * user_data)
{
	GtkSpell *spell = user_data;

	g_free(spell->lang);
	spell->lang = g_strdup(lang_tag);
}

static gboolean
gtkspell_set_language_internal(GtkSpell *spell, const gchar *lang, GError **error) {
	EnchantDict *dict;

	if (lang == NULL) {
		lang = g_getenv("LANG");
		if (lang) {
			if ((strcmp(lang, "C") == 0) || (strcmp(lang, "c") == 0))
				lang = NULL;
			else if (lang[0] == 0)
				lang = NULL;
		}
	}

	if (!lang)
		lang = "en";

	dict = enchant_broker_request_dict(broker, lang);

	if (!dict) {
		g_set_error(error, GTKSPELL_ERROR, GTKSPELL_ERROR_BACKEND,
			_("enchant error for language: %s"), lang);
		return FALSE;
	}

	if (spell->speller)
		enchant_broker_free_dict(broker, spell->speller);
	spell->speller = dict;

	enchant_dict_describe(dict, _set_lang_from_dict, spell);

	return TRUE;
}

/**
 * gtkspell_set_language:
 * @spell:  The #GtkSpell object.
 * @lang: The language to use, in a form enchant understands (it appears to
 * be a locale specifier?).
 * @error: Return location for error.
 *
 * Set the language on @spell to @lang, possibily returning an error in
 * @error.
 *
 * Returns: FALSE if there was an error.
 */
gboolean
gtkspell_set_language(GtkSpell *spell, const gchar *lang, GError **error) {
	gboolean ret;

	if (error)
		g_return_val_if_fail(*error == NULL, FALSE);

	ret = gtkspell_set_language_internal(spell, lang, error);
	if (ret)
		gtkspell_recheck_all(spell);

	return ret;
}

/**
 * gtkspell_recheck_all:
 * @spell:  The #GtkSpell object.
 *
 * Recheck the spelling in the entire buffer.
 */
void
gtkspell_recheck_all(GtkSpell *spell) {
	GtkTextIter start, end;

	gtk_text_buffer_get_bounds(spell->buffer, &start, &end);

	check_range(spell, spell->buffer, start, end, TRUE);
}

/* changes the buffer
 * a NULL buffer is acceptable and will only release the current one */
static void
gtkspell_set_buffer(GtkSpell *spell, GtkTextBuffer *buffer)
{
	GtkTextTagTable *tagtable;
	GtkTextIter start, end;

	if (spell->buffer) {
		g_signal_handlers_disconnect_matched(spell->buffer,
				G_SIGNAL_MATCH_DATA,
				0, 0, NULL, NULL,
				spell);

		tagtable = gtk_text_buffer_get_tag_table(spell->buffer);

		gtk_text_buffer_get_bounds(spell->buffer, &start, &end);
		gtk_text_buffer_remove_tag(spell->buffer, spell->tag_highlight, &start, &end);
		spell->tag_highlight = NULL;

		gtk_text_buffer_delete_mark(spell->buffer, spell->mark_insert_start);
		spell->mark_insert_start = NULL;
		gtk_text_buffer_delete_mark(spell->buffer, spell->mark_insert_end);
		spell->mark_insert_end = NULL;
		gtk_text_buffer_delete_mark(spell->buffer, spell->mark_click);
		spell->mark_click = NULL;

		g_object_unref (spell->buffer);
	}

	spell->buffer = buffer;

	if (spell->buffer) {
		g_object_ref (spell->buffer);

		g_signal_connect(G_OBJECT(spell->buffer),
				"insert-text",
				G_CALLBACK(insert_text_before), spell);
		g_signal_connect_after(G_OBJECT(spell->buffer),
				"insert-text",
				G_CALLBACK(insert_text_after), spell);
		g_signal_connect_after(G_OBJECT(spell->buffer),
				"delete-range",
				G_CALLBACK(delete_range_after), spell);
		g_signal_connect(G_OBJECT(spell->buffer),
				"mark-set",
				G_CALLBACK(mark_set), spell);

		tagtable = gtk_text_buffer_get_tag_table(spell->buffer);
		spell->tag_highlight = gtk_text_tag_table_lookup(tagtable, GTKSPELL_MISSPELLED_TAG);

		if (spell->tag_highlight == NULL) {
			spell->tag_highlight = gtk_text_buffer_create_tag(spell->buffer,
					GTKSPELL_MISSPELLED_TAG,
#ifdef HAVE_PANGO_UNDERLINE_ERROR
					"underline", PANGO_UNDERLINE_ERROR,
#else
					"foreground", "red", 
					"underline", PANGO_UNDERLINE_SINGLE,
#endif
					NULL);
		}

		/* we create the mark here, but we don't use it until text is
		 * inserted, so we don't really care where iter points.  */
		gtk_text_buffer_get_bounds(spell->buffer, &start, &end);
		spell->mark_insert_start = gtk_text_buffer_create_mark(spell->buffer,
				"gtkspell-insert-start",
				&start, TRUE);
		spell->mark_insert_end = gtk_text_buffer_create_mark(spell->buffer,
				"gtkspell-insert-end",
				&start, TRUE);
		spell->mark_click = gtk_text_buffer_create_mark(spell->buffer,
				"gtkspell-click",
				&start, TRUE);
			
		spell->deferred_check = FALSE;

		/* now check the entire text buffer. */
		gtkspell_recheck_all(spell);
	}
}

static void
buffer_changed (GtkTextView *view, GParamSpec *pspec, GtkSpell *spell)
{
	gtkspell_set_buffer(spell, gtk_text_view_get_buffer(view));
}

/**
 * gtkspell_new_attach:
 * @view: The #GtkTextView to attach to.
 * @lang: The language to use, in a form pspell understands (it appears to
 * be a locale specifier?).
 * @error: Return location for error.
 *
 * Create a new #GtkSpell object attached to @view with language @lang.
 *
 * Returns: a new #GtkSpell object, or %NULL on error.
 */
GtkSpell*
gtkspell_new_attach(GtkTextView *view, const gchar *lang, GError **error) {
	GtkSpell *spell;

#ifdef ENABLE_NLS
	bindtextdomain(PACKAGE, LOCALEDIR);
	bind_textdomain_codeset(PACKAGE, "UTF-8");
#endif

	if (error)
		g_return_val_if_fail(*error == NULL, NULL);

	spell = g_object_get_data(G_OBJECT(view), GTKSPELL_OBJECT_KEY);
	g_assert(spell == NULL);

	/* We don't need to worry about thread safety.
	 * Stuff shouldn't be attaching to a GtkTextView from anything other
	 * than the mainloop thread */
	if (!broker) {
		broker = enchant_broker_init();
		broker_ref_cnt = 0;
	}
	broker_ref_cnt++;


	/* attach to the widget */
	spell = g_new0(GtkSpell, 1);
	spell->view = view;
	if (!gtkspell_set_language_internal(spell, lang, error)) {
		broker_ref_cnt--;
		if (broker_ref_cnt == 0) {
			enchant_broker_free(broker);
			broker = NULL;
		}
		g_free(spell);
		return NULL;
	}
	g_object_set_data(G_OBJECT(view), GTKSPELL_OBJECT_KEY, spell);

	g_signal_connect_swapped(G_OBJECT(view), "destroy",
			G_CALLBACK(gtkspell_free), spell);
	g_signal_connect(G_OBJECT(view), "button-press-event",
			G_CALLBACK(button_press_event), spell);
	g_signal_connect(G_OBJECT(view), "populate-popup",
			G_CALLBACK(populate_popup), spell);
	g_signal_connect(G_OBJECT(view), "popup-menu",
			G_CALLBACK(popup_menu_event), spell);
	g_signal_connect(G_OBJECT(view), "notify::buffer",
			G_CALLBACK(buffer_changed), spell);

	spell->buffer = NULL;
	gtkspell_set_buffer(spell, gtk_text_view_get_buffer(view));

	return spell;
}

static void
gtkspell_free(GtkSpell *spell) {

	gtkspell_set_buffer(spell, NULL);

	if (broker) {
		if (spell->speller) {
			enchant_broker_free_dict(broker, spell->speller);
		}
		broker_ref_cnt--;
		if (broker_ref_cnt == 0) {
			enchant_broker_free(broker);
			broker = NULL;
		}
	}
	g_signal_handlers_disconnect_matched(spell->view,
			G_SIGNAL_MATCH_DATA,
			0, 0, NULL, NULL,
			spell);
	g_free(spell->lang);
	g_free(spell);
}

/**
 * gtkspell_get_from_text_view:
 * @view: A #GtkTextView.
 *
 * Retrieves the #GtkSpell object attached to a text view.
 *
 * Returns: the #GtkSpell object, or %NULL if there is no #GtkSpell
 * attached to @view.
 */
GtkSpell*
gtkspell_get_from_text_view(GtkTextView *view) {
	return g_object_get_data(G_OBJECT(view), GTKSPELL_OBJECT_KEY);
}

/**
 * gtkspell_detach:
 * @spell: A #GtkSpell.
 *
 * Detaches this #GtkSpell from its text view.  Use
 * gtkspell_get_from_text_view() to retrieve a GtkSpell from a
 * #GtkTextView.
 */
void
gtkspell_detach(GtkSpell *spell) {
	g_return_if_fail(spell != NULL);

	g_object_set_data(G_OBJECT(spell->view), GTKSPELL_OBJECT_KEY, NULL);
	gtkspell_free(spell);
}

/**
 * gtkspell_get_suggestions_menu:
 * @iter: Textiter of position in buffer to be corrected if necessary.
 *
 * Retrieves a submenu of replacement spellings, or NULL if the word at @iter is
 * not misspelt.
 *
 * Returns: the #GtkMenu widget, or %NULL if there is no need for a menu
 */
GtkWidget*
gtkspell_get_suggestions_menu(GtkSpell *spell, GtkTextIter *iter) {
	GtkWidget *submenu = NULL;
	GtkTextIter start, end;

	g_return_val_if_fail(spell != NULL, NULL);

	/* avoid an empty submenu when enchant is not working properly */
	if (!spell->speller)
		return NULL;

	start = *iter;
	/* use the same lazy test, with same risk, as does the default menu arrangement */
	if (gtk_text_iter_has_tag(&start, spell->tag_highlight)) {
		/* word was mis-spelt */
		gchar *badword;
		/* in case a fix is requested, move the attention-point */
		gtk_text_buffer_move_mark(spell->buffer, spell->mark_click, iter);
		if (!gtk_text_iter_starts_word(&start))
			gtk_text_iter_backward_word_start(&start);
		end = start;
		if (gtk_text_iter_inside_word(&end))
			gtk_text_iter_forward_word_end(&end);
		badword = gtk_text_buffer_get_text (spell->buffer, &start, &end, FALSE);

		submenu = build_suggestion_menu (spell, spell->buffer, badword);
		gtk_widget_show (submenu);

		g_free (badword);
	}
	return submenu;
}
