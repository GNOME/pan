add_library(data-impl
 article-filter.cc
 data-io.cc
 data-impl.cc
 groups.cc
 headers.cc
 header-filter.cc
 header-rules.cc
 profiles.cc
 server.cc
 my-tree.cc
 task-archive.cc
 xover.cc
)

find_package(SQLiteCpp REQUIRED)
target_link_libraries(data-impl PRIVATE SQLiteCpp)

find_package(fmt REQUIRED CONFIG)
target_link_libraries(data-impl PRIVATE fmt::fmt)

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

set(data-impl-test-programs article-test header-filter-test header-rules-test
  header-score-test ghost-article-test my-tree-test)

foreach(test ${data-impl-test-programs})
  add_executable(${test} ${test}.cc)
  # Sanity checking
  # https://cmake.org/cmake/help/latest/command/add_compile_options.html
  target_compile_options(${test} PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")
  target_link_libraries(${test}
    data-impl
    pan-cc-gui
    pan-c-gui
    tasks
    data
    usenet-utils
    generalutils
    uulib
    SQLiteCpp
    PkgConfig::CPPUNIT)
  add_test(NAME ${test}-test COMMAND ${test} WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endforeach()
