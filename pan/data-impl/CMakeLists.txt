add_library(data-impl
 article-filter.cc
 article-rules.cc
 data-io.cc
 data-impl.cc
 groups.cc
 headers.cc
 profiles.cc
 server.cc
 my-tree.cc
 task-archive.cc
 xover.cc
)

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(data-impl PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")

target_link_libraries(data-impl PUBLIC PkgConfig::GTK3)
target_link_libraries(data-impl PUBLIC PkgConfig::GMIME)

if(WANT_GNUTLS)
  target_link_libraries(data-impl PUBLIC PkgConfig::GNUTLS)
endif()

if(WANT_GKR)
  target_link_libraries(data-impl PUBLIC PkgConfig::LIBSECRET)
  target_link_libraries(data-impl PUBLIC PkgConfig::GCR3)
endif()
