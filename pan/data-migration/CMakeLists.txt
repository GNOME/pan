add_library(data-migration
 article-filter.cc
 rules-filter.cc
 data-io.cc
 data-migration.cc
 groups.cc
 headers.cc
 profiles.cc
 server.cc
 my-tree.cc
 task-archive.cc
 xover.cc
)

find_package(SQLiteCpp REQUIRED)
target_link_libraries(data-migration PRIVATE SQLiteCpp)

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(data-migration PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")

target_link_libraries(data-migration PUBLIC PkgConfig::GTK3)
target_link_libraries(data-migration PUBLIC PkgConfig::GMIME)

if(WANT_GNUTLS)
  target_link_libraries(data-migration PUBLIC PkgConfig::GNUTLS)
endif()

if(WANT_GKR)
  target_link_libraries(data-migration PUBLIC PkgConfig::LIBSECRET)
  target_link_libraries(data-migration PUBLIC PkgConfig::GCR3)
endif()
