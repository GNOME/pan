SUBDIRS = uulib pan po

DIST_SUBDIRS = uulib pan po help

if ENABLE_MANUAL

SUBDIRS += help

endif

desktopdir = $(datadir)/applications
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)
desktop_in_files = pan.desktop.in
@INTLTOOL_DESKTOP_RULE@

@INTLTOOL_XML_RULE@
appdatadir = $(datadir)/appdata
appdata_DATA = $(appdata_in_files:.xml.in=.xml)
appdata_in_files = pan.appdata.xml.in

CLEANFILES = \
 $(srcdir)/pan.appdata.xml \
 $(appdata_DATA)

DISTCLEANFILES = \
 $(srcdir)/intltool-extract \
 $(srcdir)/intltool-merge \
 $(srcdir)/intltool-update \
 $(desktop_DATA)

MAINTAINERCLEANFILES = \
 $(srcdir)/INSTALL \
 $(srcdir)/aclocal.m4 \
 $(srcdir)/config.guess \
 $(srcdir)/config.h.in \
 $(srcdir)/config.sub \
 $(srcdir)/configure \
 $(srcdir)/depcomp \
 $(srcdir)/install-sh \
 $(srcdir)/missing \
 $(srcdir)/mkinstalldirs \
 $(srcdir)/po/Makefile.in.in \
 `find "$(srcdir)" -type f -name Makefile.in -print` \
 $(NULL)

EXTRA_DIST = \
 COPYING-DOCS \
 README \
 README.windows \
 Pan.ico \
 pan.png \
 $(appdata_in_files) \
 $(desktop_in_files) \
 intltool-extract.in intltool-merge.in intltool-update.in \
 pan-git.version

icon_DATA = pan.png
icondir = $(datadir)/pixmaps

