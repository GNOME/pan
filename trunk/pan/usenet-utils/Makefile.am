AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@

noinst_LIBRARIES = libusenetutils.a

libusenetutils_a_SOURCES = \
 gnksa.cc \
 message-check.cc \
 mime-utils.cc \
 numbers.cc \
 text-massager.cc \
 utf8-utils.cc \
 url-find.cc

noinst_HEADERS = \
 defgroup.h \
 gnksa.h \
 message-check.h \
 mime-utils.h \
 numbers.h \
 text-massager.h \
 utf8-utils.h \
 url-find.h

noinst_PROGRAMS = \
 gnksa-test \
 message-check-test \
 numbers-test \
 text-massager-test \
 url-find-test

TESTS = $(noinst_PROGRAMS)
TEST_LDADD = ./libusenetutils.a ../general/libgeneralutils.a @GMIME_LIBS@ @GLIB_LIBS@ @PCRE_LIBS@
gnksa_test_SOURCES = gnksa-test.cc
gnksa_test_LDADD = $(TEST_LDADD)
message_check_test_SOURCES = message-check-test.cc
message_check_test_LDADD = $(TEST_LDADD)
numbers_test_SOURCES = numbers-test.cc
numbers_test_LDADD = $(TEST_LDADD)
text_massager_test_SOURCES = text-massager-test.cc
text_massager_test_LDADD = $(TEST_LDADD)
url_find_test_SOURCES = url-find-test.cc
url_find_test_LDADD = $(TEST_LDADD)
