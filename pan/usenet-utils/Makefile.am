AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@

noinst_LIBRARIES = libusenetutils.a

libusenetutils_a_SOURCES = \
 filter-info.cc \
 gnksa.cc \
 message-check.cc \
 mime-utils.cc \
 numbers.cc \
 scorefile.cc \
 text-massager.cc \
 url-find.cc 

noinst_HEADERS = \
 defgroup.h \
 filter-info.h \
 gnksa.h \
 message-check.h \
 mime-utils.h \
 numbers.h \
 scorefile.h \
 text-massager.h \
 url-find.h 

noinst_PROGRAMS = \
 gnksa-test \
 message-check-test \
 numbers-test \
 scorefile-test \
 text-massager-test \
 url-find-test

TESTS = $(noinst_PROGRAMS)
TEST_LDADD = ./libusenetutils.a ../general/libgeneralutils.a @GMIME_LIBS@ @GLIB_LIBS@
gnksa_test_SOURCES = gnksa-test.cc
gnksa_test_LDADD = $(TEST_LDADD)
message_check_test_SOURCES = message-check-test.cc
message_check_test_LDADD = $(TEST_LDADD)
numbers_test_SOURCES = numbers-test.cc
numbers_test_LDADD = $(TEST_LDADD)
scorefile_test_SOURCES = scorefile-test.cc
scorefile_test_LDADD = $(TEST_LDADD)
text_massager_test_SOURCES = text-massager-test.cc
text_massager_test_LDADD = $(TEST_LDADD)
url_find_test_SOURCES = url-find-test.cc
url_find_test_LDADD = $(TEST_LDADD)

