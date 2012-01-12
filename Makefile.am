SUBDIRS = po uulib pan
SUBDIRS += $(HELP_SUBDIR)


@INTLTOOL_DESKTOP_RULE@

DESKTOP_IN_FILES=pan.desktop.in
DESKTOP_FILES=$(DESKTOP_IN_FILES:.desktop.in=.desktop)

DISTCLEANFILES = \
 gnome-doc-utils.make \
 intltool-extract \
 intltool-merge \
 intltool-update \
 pan.desktop

EXTRA_DIST = \
 README \
 README.windows \
 README.mingw \
 ChangeLog \
 Pan.ico \
 gnome-doc-utils.make \
 pan.spec \
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
