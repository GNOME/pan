SUBDIRS = . uulib pan po

DIST_SUBDIRS = . uulib pan po help

if ENABLE_MANUAL
SUBDIRS += help
endif

org.gnome.pan.desktop: org.gnome.pan.desktop.in
	$(AM_V_GEN)$(MSGFMT) --desktop --template $< -d $(top_srcdir)/po -o $@
desktopdir = $(datadir)/applications
desktop_DATA = org.gnome.pan.desktop

org.gnome.pan.metainfo.xml: org.gnome.pan.metainfo.xml.in
	$(AM_V_GEN)$(MSGFMT) --xml --template $< -d $(top_srcdir)/po -o $@
appdatadir = $(datadir)/metainfo
appdata_DATA = org.gnome.pan.metainfo.xml

man_MANS = pan.1

CLEANFILES = \
 $(appdata_DATA) \
 $(desktop_DATA)

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
 README.org \
 README.windows \
 org.gnome.pan.desktop.in \
 org.gnome.pan.metainfo.xml.in \
 pan-git.version \
 $(man_MANS) \
 $(NULL)

CPPCHECK_CMD = cppcheck \
    -j 4 \
    --force \
    --inline-suppr \
    --error-exitcode=1 \
    -I .

CPPCHECK_DIRS = \
    .

CPPCHECK_OUT_PREFIX = $(top_builddir)/cppcheck-

CPPCHECK_OUT_EXT = log.txt

cppcheck:
	echo $(CPPCHECK_CMD) --enable=all $(CPPCHECK_DIRS)
	$(CPPCHECK_CMD) --enable=all $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)all.$(CPPCHECK_OUT_EXT)

cppcheck-information:
	$(CPPCHECK_CMD) --enable=information $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)information.$(CPPCHECK_OUT_EXT)

cppcheck-performance:
	$(CPPCHECK_CMD) --enable=performance $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)performance.$(CPPCHECK_OUT_EXT)

cppcheck-portability:
	$(CPPCHECK_CMD) --enable=portability $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)portability.$(CPPCHECK_OUT_EXT)

cppcheck-style:
	$(CPPCHECK_CMD) --enable=style $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)style.$(CPPCHECK_OUT_EXT)

cppcheck-warning:
	$(CPPCHECK_CMD) --enable=warning $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)warning.$(CPPCHECK_OUT_EXT)

# Separate because unusedFunction is disabled with -j
cppcheck-unusedFunction:
	$(CPPCHECK_CMD) --enable=unusedFunction $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)unusedFunction.$(CPPCHECK_OUT_EXT)

cppcheck-missingInclude:
	$(CPPCHECK_CMD) --enable=missingInclude $(CPPCHECK_DIRS) --output-file=$(CPPCHECK_OUT_PREFIX)missingInclude.$(CPPCHECK_OUT_EXT)
