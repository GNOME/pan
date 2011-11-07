AM_CPPFLAGS = -I@top_srcdir@ @GMIME_CFLAGS@ @GLIB_CFLAGS@ @OPENSSL_CFLAGS@

AM_LDFLAGS = ../../uulib/libuu.a -lz @OPENSSL_LIBS@

noinst_LIBRARIES = libtasks.a

libtasks_a_SOURCES = \
  decoder.cc \
  encoder.cc \
  task.cc \
  task-article.cc \
  task-groups.cc \
  task-post.cc \
  task-xover.cc \
  task-upload.cc \
  nntp.cc \
  nzb.cc \
  queue.cc \
  upload-queue.cc \
  socket.cc \
  cert-store.cc \
  socket-impl-main.cc \
  socket-impl-openssl.cc \
  socket-impl-gio.cc \
  socket-impl-scripted.cc \
  nntp-pool.cc

noinst_HEADERS = \
  adaptable-set.cc \
  adaptable-set.h \
  decoder.h \
  encoder.h \
  defgroup.h \
  health.h \
  task.h \
  task-article.h \
  task-groups.h \
  task-post.h \
  task-upload.h \
  task-weak-ordering.h \
  task-xover.h \
  nntp.h  \
  nzb.h  \
  queue.h  \
  upload-queue.h \
  socket.h \
  cert-store.h \
  socket-impl-main.h \
  socket-impl-openssl.cc \
  socket-impl-gio.h \
  socket-impl-scripted.h \
  nntp-pool.h

#noinst_PROGRAMS = \
#  adaptable-set-test \
#  nzb-test

#TESTS = $(noinst_PROGRAMS)

#TEST_LDADD = ./libtasks.a \
# ../data/libdata.a \
# ../usenet-utils/libusenetutils.a \
# ../general/libgeneralutils.a \
# ../../uulib/libuu.a \
# @GMIME_LIBS@ @GLIB_LIBS@

#adaptable_set_test_SOURCES = adaptable-set-test.cc
#nzb_test_SOURCES = nzb-test.cc
#nzb_test_LDADD = $(TEST_LDADD)

# FIXME: these need to be moved up the food chain past data-impl
#
#noinst_PROGRAMS = \
#   bootstrap-groups \
#   bootstrap-headers \
#   nntp-test \
#   nntp-pool-test \
#   queue-test
#
#TESTS = \
#   nntp-test \
#   nntp-pool-test \
#   queue-test
#
#TEST_LDADD = ./libtasks.a ../data/libdata.a ../usenet-utils/libusenetutils.a ../general/libgeneralutils.a ../../gmime/libgmime.a $(GLIB_LIBS)
#bootstrap_groups_SOURCES = bootstrap-groups.cc
#bootstrap_groups_LDADD = $(TEST_LDADD)
#bootstrap_headers_SOURCES = bootstrap-headers.cc
#bootstrap_headers_LDADD = $(TEST_LDADD)
#nntp_test_SOURCES = nntp-test.cc
#nntp_test_LDADD = $(TEST_LDADD)
#nntp_pool_test_SOURCES = nntp-pool-test.cc
#nntp_pool_test_LDADD = $(TEST_LDADD)
#queue_test_SOURCES = queue-test.cc
#queue_test_LDADD = $(TEST_LDADD)
