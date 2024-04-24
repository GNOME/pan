add_library(usenet-utils
 filter-info.cc
 rules-info.cc
 gnksa.cc
 message-check.cc
 mime-utils.cc
 numbers.cc
 scorefile.cc
 ssl-utils.cc
 text-massager.cc
 url-find.cc
 blowfish.cc
 gpg.cc
)


# TODO: fix  message-check-test and add it to this list
set(test-programs gnksa-test numbers-test scorefile-test text-massager-test url-find-test)

target_link_libraries(usenet-utils PUBLIC PkgConfig::GTK3)
target_link_libraries(usenet-utils PUBLIC PkgConfig::GMIME)
target_link_libraries(usenet-utils PUBLIC PkgConfig::GLIB)

if(WANT_GNUTLS)
  target_link_libraries(usenet-utils PUBLIC PkgConfig::GNUTLS)
endif()

foreach(test ${test-programs})
  add_executable(${test} ${test}.cc)
  # Sanity checking
  # https://cmake.org/cmake/help/latest/command/add_compile_options.html
  target_compile_options(${test} PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")
  target_link_libraries(${test} usenet-utils generalutils pan-cc-gui)
  add_test(NAME ${test}-test COMMAND ${test})
endforeach()
