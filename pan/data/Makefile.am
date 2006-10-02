AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@

SUBDIRS = decode-test-cache

noinst_LIBRARIES = libdata.a

libdata_a_SOURCES = \
 article.cc \
 article-cache.cc \
 data.cc \
 filter-info.cc \
 scorefile.cc \
 xref.cc

noinst_HEADERS = \
 article.h \
 article-cache.h \
 data.h \
 defgroup.h \
 filter-info.h \
 scorefile.h \
 server-info.h \
 xref.h 

noinst_PROGRAMS = \
 xref-test \
 scorefile-test

#dnl decode-test 
#dnl decode_test_SOURCES = decode-test.cc
#dnl decode_test_LDADD = $(TEST_LDADD)

TESTS = $(noinst_PROGRAMS)
TEST_LDADD = ./libdata.a ../usenet-utils/libusenetutils.a ../general/libgeneralutils.a @GMIME_LIBS@ @GLIB_LIBS@ @PCRE_LIBS@
xref_test_SOURCES = xref-test.cc
xref_test_LDADD = $(TEST_LDADD)
scorefile_test_SOURCES = scorefile-test.cc
scorefile_test_LDADD = $(TEST_LDADD)
