add_library(tasks
  decoder.cc
  encoder.cc
  task.cc
  task-article.cc
  task-groups.cc
  task-post.cc
  task-xover.cc
  task-upload.cc
  nntp.cc
  nzb.cc
  queue.cc
  upload-queue.cc
  socket.cc
  socket-impl-main.cc
  socket-impl-openssl.cc
  socket-impl-gio.cc
  socket-impl-scripted.cc
  nntp-pool.cc
)

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(tasks PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")

target_link_libraries(tasks PUBLIC PkgConfig::GTK3)

target_link_libraries(tasks PUBLIC PkgConfig::GMIME)

if(Iconv_FOUND)
  my_target_system_lib(tasks PUBLIC Iconv)
endif()

if(WANT_GNUTLS)
  target_link_libraries(tasks PUBLIC PkgConfig::GNUTLS)
endif()
