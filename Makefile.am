SUBDIRS = po uulib pan

@INTLTOOL_DESKTOP_RULE@

DESKTOP_IN_FILES=pan.desktop.in
DESKTOP_FILES=$(DESKTOP_IN_FILES:.desktop.in=.desktop)

DISTCLEANFILES = \
 $(srcdir)/intltool-extract \
 $(srcdir)/intltool-merge \
 $(srcdir)/intltool-update \
 $(srcdir)/pan.desktop

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
 README \
 README.windows \
 README.mingw \
 ChangeLog \
 Pan.ico \
 pan.png \
 $(DESKTOP_IN_FILES) \
 $(DESKTOP_FILES) \
 intltool-extract.in intltool-merge.in intltool-update.in \
 pan_git_rev

DISTCHECK_CONFIGURE_FLAGS="--disable-scrollkeeper"

Productivitydir = $(datadir)/applications
Productivity_DATA = $(DESKTOP_FILES)

icon_DATA = pan.png
icondir = $(datadir)/pixmaps

