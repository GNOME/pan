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
