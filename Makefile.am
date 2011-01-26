SUBDIRS = po uulib pan

@INTLTOOL_DESKTOP_RULE@

DESKTOP_IN_FILES=pan.desktop.in
DESKTOP_FILES=$(DESKTOP_IN_FILES:.desktop.in=.desktop)

DISTCLEANFILES = \
 intltool-extract \
 intltool-merge \
 intltool-update \
 pan.desktop \
 gnome-doc-utils.make

DISTCHECK_CONFIGURE_FLAGS="--disable-scrollkeeper"

EXTRA_DIST = \
 README \
 README.windows \
 README.mingw \
 ChangeLog \
 Pan.ico \
 pan.spec \
 pan.png \
 $(DESKTOP_IN_FILES) \
 $(DESKTOP_FILES) \
 intltool-extract.in intltool-merge.in intltool-update.in \
 gnome-doc-utils.make

Productivitydir = $(datadir)/applications
Productivity_DATA = $(DESKTOP_FILES)

icon_DATA = pan.png
icondir = $(datadir)/pixmaps
