add_library(data-impl
 article-filter.cc
 rules-filter.cc
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
