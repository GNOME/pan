SUBDIRS = . uulib pan po

DIST_SUBDIRS = . uulib pan po help

if ENABLE_MANUAL
SUBDIRS += help
endif

pan.desktop: pan.desktop.in
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@
desktopdir = $(datadir)/applications
desktop_DATA = pan.desktop

pan.appdata.xml: pan.appdata.xml.in
	$(AM_V_GEN)$(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@
appdatadir = $(datadir)/appdata
appdata_DATA = pan.appdata.xml

man_MANS = pan.1

CLEANFILES = \
 pan.appdata.xml \
 pan.desktop \
 $(appdata_DATA)

DISTCLEANFILES = \
 pan.appdata.xml \
 pan.desktop

MAINTAINERCLEANFILES = \
 $(srcdir)/aclocal.m4 \
 $(srcdir)/config.guess \
 $(srcdir)/config.h.in \
 $(srcdir)/config.sub \
 $(srcdir)/configure \
 $(srcdir)/depcomp \
 $(srcdir)/install-sh \
 $(srcdir)/missing \
 $(srcdir)/mkinstalldirs \
 `find "$(srcdir)" -type f -name Makefile.in -print` \
 $(NULL)

EXTRA_DIST = \
 COPYING-DOCS \
 README \
 README.windows \
 pan.appdata.xml.in \
 pan.desktop.in \
 pan-git.version \
 $(man_MANS) \
 $(NULL)

