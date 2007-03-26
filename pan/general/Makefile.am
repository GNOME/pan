AM_CPPFLAGS = -I@top_srcdir@ @GLIB_CFLAGS@ @PCRE_CFLAGS@

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
 worker-pool.cc

noinst_HEADERS = \
 debug.h \
 defgroup.h \
 e-util.h \
 file-util.h \
 foreach.h \
 line-reader.h \
 locking.h \
 log.h \
 map-vector.h \
 messages.h \
 progress.h \
 quark.h \
 sorted-vector.h \
 string-view.h \
 test.h \
 text-match.h \
 time-elapsed.h \
 worker-pool.h 

noinst_PROGRAMS = \
  progress-test \
  quark-test \
  string-view-test \
  text-match-test

TESTS = $(noinst_PROGRAMS)
TEST_LDADD = ./libgeneralutils.a @GLIB_LIBS@ @PCRE_LIBS@
progress_test_SOURCES = progress-test.cc
progress_test_LDADD = $(TEST_LDADD)
quark_test_SOURCES = quark-test.cc
quark_test_LDADD = $(TEST_LDADD)
string_view_test_SOURCES = string-view-test.cc
string_view_test_LDADD = $(TEST_LDADD)
text_match_test_SOURCES = text-match-test.cc
text_match_test_LDADD = $(TEST_LDADD)
