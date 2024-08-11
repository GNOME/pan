add_library(generalutils
 article_number.cc
 debug.cc
 e-util.cc
 file-util.cc
 log.cc
 line-reader.cc
 progress.cc
 quark.cc
 string-view.cc
 text-match.cc
 utf8-utils.cc
 worker-pool.cc
 compression.cc
)

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(generalutils PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")

# see https://cmake.org/cmake/help/latest/module/FindZLIB.html
if(ZLIB_FOUND)
  my_target_system_lib(generalutils PUBLIC ZLIB)
endif()

target_link_libraries(generalutils PUBLIC PkgConfig::GMIME)
target_link_libraries(generalutils PUBLIC PkgConfig::GTK3)
