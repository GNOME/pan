AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@ @GNUTLS_CFLAGS@ \
						@LIBGNOME_KEYRING_1_CFLAGS@ @GTK_CFLAGS@

AM_LDFLAGS = @LIBGNOME_KEYRING_1_LIBS@ @GTK_LIBS@

noinst_LIBRARIES = libpandata.a

libpandata_a_SOURCES = \
 article-filter.cc \
 rules-filter.cc \
 data-io.cc \
 data-impl.cc \
 groups.cc \
 headers.cc \
 profiles.cc \
 server.cc \
 my-tree.cc \
 task-archive.cc \
 xover.cc


noinst_HEADERS = \
 article-filter.h \
 rules-filter.h \
 data-impl.h \
 data-io.h \
 defgroup.h \
 profiles.h \
 memchunk.h

#noinst_PROGRAMS = \
# add-server \
# headers-test \
# speed-test-load-group

#TESTS = \
# add-server \
# headers-test

#TEST_LDADD = \
# ./libpandata.a \
# ../tasks/libtasks.a \
# ../data/libdata.a \
# ../usenet-utils/libusenetutils.a \
# ../general/libgeneralutils.a \
# ../../uulib/libuu.a \
# @GMIME_LIBS@ @GLIB_LIBS@
#add_server_SOURCES = add-server.cc
#add_server_LDADD = $(TEST_LDADD)
#headers_test_SOURCES = headers-test.cc
#headers_test_LDADD = $(TEST_LDADD)
#speed_test_load_group_SOURCES = speed-test-load-group.cc
#speed_test_load_group_LDADD = $(TEST_LDADD)
