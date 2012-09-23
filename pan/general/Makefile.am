AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@ @GTK_CFLAGS@
AM_LDFLAGS = @GTK_LIBS@

noinst_LIBRARIES = libgeneralutils.a

libgeneralutils_a_SOURCES = \
 debug.cc \
 e-util.cc \
 file-util.cc \
 log.cc \
 line-reader.cc \
 progress.cc \
 quark.cc \
 string-view.cc \
 text-match.cc \
 utf8-utils.cc \
 worker-pool.cc

noinst_HEADERS = \
 debug.h \
 defgroup.h \
 e-util.h \
 file-util.h \
 line-reader.h \
 locking.h \
 log.h \
 macros.h \
 map-vector.h \
 messages.h \
 progress.h \
 quark.h \
 sorted-vector.h \
 string-view.h \
 test.h \
 text-match.h \
 time-elapsed.h \
 utf8-utils.h \
 worker-pool.h \
 singleton-template.h

#noinst_PROGRAMS = \
#  progress-test \
#  quark-test \
#  string-view-test \
#  text-match-test

#TESTS = $(noinst_PROGRAMS)
#TEST_LDADD = ./libgeneralutils.a @GMIME_CFLAGS@ @GLIB_LIBS@
#progress_test_SOURCES = progress-test.cc
#progress_test_LDADD = $(TEST_LDADD)
#quark_test_SOURCES = quark-test.cc
#quark_test_LDADD = $(TEST_LDADD)
#string_view_test_SOURCES = string-view-test.cc
#string_view_test_LDADD = $(TEST_LDADD)
#text_match_test_SOURCES = text-match-test.cc
#text_match_test_LDADD = $(TEST_LDADD)
