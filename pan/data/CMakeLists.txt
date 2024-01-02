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
