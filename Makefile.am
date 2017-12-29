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

dbusservicedir = $(datadir)/dbus-1/services
dbusservice_DATA = news.pan.NZB.service

news.pan.NZB.service: Makefile
	$(AM_V_GEN) (echo '[D-BUS Service]'; \
		echo 'Name=news.pan.NZB'; \
		echo 'Exec=${bindir}/pan') > $@.tmp && \
		mv $@.tmp $@

man_MANS = pan.1

CLEANFILES = \
 $(appdata_DATA) \
 $(desktop_DATA) \
 $(dbusservice_DATA)

DISTCLEANFILES = \
 README.windows \
 pan-git.version

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
 news.pan.NZB.service \
 pan.appdata.xml.in \
 pan.desktop.in \
 pan-git.version \
 $(man_MANS) \
 $(NULL)

