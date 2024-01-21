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

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(usenet-utils PRIVATE "-Wreorder" "-Wzero-as-null-pointer-constant")

# TODO: fix  message-check-test and add it to this list
set(test-programs gnksa-test numbers-test scorefile-test text-massager-test url-find-test)

foreach(test ${test-programs})
  add_executable(${test} ${test}.cc)
  target_link_libraries(${test} usenet-utils generalutils ${GMIME_LIBRARIES} ${GLIB_LIBRARIES})
  add_test(NAME ${test}-test COMMAND ${test})
endforeach()
