set(SOURCES 
 article.cc
 article-cache.cc
 encode-cache.cc
 cert-store.cc
 data.cc
 parts.cc
 xref.cc
)

add_library(data ${SOURCES})

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(data PRIVATE "-Wreorder" "-Wzero-as-null-pointer-constant")

# this test is broken
# add_executable(article-test article-test.cc)
# add_test(NAME article COMMAND article-test)
