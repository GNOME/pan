AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@ @GTK_CFLAGS@
LDADD = @GTK_LIBS@

AM_CPPFLAGS += -DDATABASE_SQLITE

#SUBDIRS = decode-test-cache

noinst_LIBRARIES = libdata.a

libdata_a_SOURCES = \
 article.cc \
 article-cache.cc \
 encode-cache.cc \
 cert-store.cc \
 data.cc \
 parts.cc \
 xref.cc

noinst_HEADERS = \
 article.h \
 article-cache.h \
 encode-cache.h \
 cert-store.h \
 data.h \
 parts.h \
 server-info.h \
 xref.h

#noinst_PROGRAMS = \
# article-test \
# xref-test

#dnl decode-test
#dnl decode_test_SOURCES = decode-test.cc
#dnl decode_test_LDADD = $(TEST_LDADD)

#TESTS = $(noinst_PROGRAMS)
#TEST_LDADD = ./libdata.a ../usenet-utils/libusenetutils.a ../general/libgeneralutils.a @GMIME_LIBS@ @GLIB_LIBS@
#xref_test_SOURCES = xref-test.cc
#xref_test_LDADD = $(TEST_LDADD)
#article_test_SOURCES = article-test.cc
#article_test_LDADD = $(TEST_LDADD)
