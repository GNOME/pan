#ifndef E_CHARSET_DIALOG_H
#define E_CHARSET_DIALOG_H

G_BEGIN_DECLS

char* e_charset_dialog (const char *title, const char *prompt,
                        const char *default_charset, GtkWindow *parent);

G_END_DECLS

#endif
