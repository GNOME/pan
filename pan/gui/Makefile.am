AM_CPPFLAGS = -I@top_srcdir@ @GTKSPELL_CFLAGS@ @GTK_CFLAGS@ @GMIME_CFLAGS@ @GLIB_CFLAGS@ -DPANLOCALEDIR=\""$(panlocaledir)"\"

noinst_LIBRARIES = libpangui.a

libpangui_a_SOURCES = \
 actions.cc \
 body-pane.cc \
 charset-picker.cc \
 dl-headers-ui.cc \
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
 save-ui.cc \
 score-add-ui.cc \
 score-view-ui.cc \
 server-ui.cc \
 sexy-icon-entry.cc \
 task-pane.cc \
 xface.c \
 url.cc

noinst_HEADERS = \
 action-manager.h \
 actions.h \
 body-pane.h \
 charset-picker.h \
 defgroup.h \
 dl-headers-ui.h \
 group-pane.h \
 group-prefs.h \
 group-prefs-dialog.h \
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
 prefs.h \
 prefs-file.h \
 prefs-ui.h \
 post-ui.h \
 post.ui.h \
 profiles-dialog.h \
 progress-view.h \
 save-ui.h \
 score-add-ui.h \
 score-view-ui.h \
 server-ui.h \
 sexy-icon-entry.h \
 tango-colors.h \
 task-pane.h \
 url.h \
 xface.h

EXTRA_DIST = \
 panrc.rc

bin_PROGRAMS = \
 pan

if HAVE_WIN32
WINRC = panrc.rc
WINRCOBJ = $(WINRC:.rc=.o)
.rc.o: ../../config.h $(srcdir)/Pan.ico
	$(WINDRES) -I . -I $(srcdir) -o $@ -i $<
else
WINRC =
WINRCOBJ =
endif
  
pan_SOURCES = gui.cc pan.cc $(WINRC)
pan_LDADD = ./libpangui.a $(WINRCOBJ) ../data-impl/libpandata.a ../tasks/libtasks.a ../data/libdata.a ../usenet-utils/libusenetutils.a ../general/libgeneralutils.a ../../uulib/libuu.a @GTKSPELL_LIBS@ @GTK_LIBS@ @GMIME_LIBS@ @GLIB_LIBS@ @PCRE_LIBS@
