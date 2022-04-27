AM_CPPFLAGS = -I@top_srcdir@ @GTKSPELL_CFLAGS@ @ENCHANT_CFLAGS@ @GTK_CFLAGS@ @GMIME_CFLAGS@ @GLIB_CFLAGS@ \
	@GNUTLS_CFLAGS@ @LIBNOTIFY_CFLAGS@ @LIBGNOME_KEYRING_1_CFLAGS@ @WEBKITGTK_CFLAGS@-DPANLOCALEDIR=\""$(panlocaledir)"\" @ZLIB_CFLAGS@

noinst_LIBRARIES = libpangui.a

libpangui_a_SOURCES = \
 actions.cc \
 body-pane.cc \
 dl-headers-ui.cc \
 e-action-combo-box.c \
 e-charset.c \
 e-charset-combo-box.c \
 e-charset-dialog.c \
 e-cte-dialog.c \
 group-pane.cc \
 group-prefs.cc \
 group-prefs-dialog.cc \
 header-pane.cc \
 hig.cc \
 log-ui.cc \
 pan-file-entry.cc \
 pan-pixbufs.c \
 pan-tree.cc \
 post-ui.cc \
 prefs.cc \
 prefs-file.cc \
 prefs-ui.cc \
 progress-view.cc \
 profiles-dialog.cc \
 render-bytes.cc \
 save-ui.cc \
 save-attach-ui.cc \
 score-add-ui.cc \
 score-view-ui.cc \
 server-ui.cc \
 task-pane.cc \
 xface.c \
 url.cc

noinst_HEADERS = \
 action-manager.h \
 actions.h \
 actions-extern.h \
 body-pane.h \
 defgroup.h \
 dl-headers-ui.h \
 e-action-combo-box.h \
 e-charset.h \
 e-charset-combo-box.h \
 e-charset-dialog.h \
 e-cte-dialog.h \
 group-pane.h \
 group-prefs.h \
 group-prefs-dialog.h \
 gtk-compat.h \
 gui.h \
 header-pane.h \
 hig.h \
 license.h \
 log-ui.h \
 pad.h \
 pan-file-entry.h \
 pan-tree.h \
 pan-ui.h \
 pan.ui.h \
 pan.ui.ssl.h \
 prefs.h \
 prefs-file.h \
 prefs-ui.h \
 post-ui.h \
 post.ui.h \
 post.ui.pgp.h \
 profiles-dialog.h \
 progress-view.h \
 render-bytes.h \
 save-ui.h \
 save-attach-ui.h \
 score-add-ui.h \
 score-view-ui.h \
 server-ui.h \
 tango-colors.h \
 task-pane.h \
 taskpane.ui.h \
 url.h \
 wait.h \
 xface.h \
 pan-colors.h

EXTRA_DIST = \
 panrc.rc

bin_PROGRAMS = \
 pan

if HAVE_WIN32
WINRC = panrc.rc
WINRCOBJ = $(WINRC:.rc=.o)
$(WINRCOBJ): pan.manifest.xml
.rc.o: ../../config.h $(srcdir)/pan/icons/Pan.ico
	$(WINDRES) -I . -I $(srcdir) -o $@ -i $<
WINLIBS = -liconv
else
WINRC =
#WINRCOBJ =
WINLIBS =
endif

pan_SOURCES = gui.cc pan.cc $(WINRC)
pan_LDADD = ./libpangui.a ../data-impl/libpandata.a ../tasks/libtasks.a ../data/libdata.a ../usenet-utils/libusenetutils.a ../general/libgeneralutils.a ../../uulib/libuu.a \
		@GTKSPELL_LIBS@ @ENCHANT_LIBS@ @GTK_LIBS@ @GMIME_LIBS@ @GLIB_LIBS@ @GNUTLS_LIBS@ @LIBNOTIFY_LIBS@ @LIBGNOME_KEYRING_1_LIBS@ @WEBKITGTK_LIBS@ @ZLIB_LIBS@ ${WINLIBS}
if HAVE_WIN32
pan_LDFLAGS = -mwindows
endif
