add_library(generalutils
 article_number.cc
 debug.cc
 e-util.cc
 file-util.cc
 group-prefs.cc
 log.cc
 log4cxx.cc
 line-reader.cc
 prefs.cc
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

set (general-tests text-match-test progress-test quark-test string-view-test)

foreach(test ${general-tests})
  add_executable(${test} ${test}.cc)
  # Sanity checking
  # https://cmake.org/cmake/help/latest/command/add_compile_options.html
  target_compile_options(${test} PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")
  target_link_libraries(${test} usenet-utils generalutils)
  add_test(NAME ${test}-test COMMAND ${test})
endforeach()
