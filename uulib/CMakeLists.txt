# collect up the source files
file(GLOB SRC_FILES "*.c")

add_library(uu STATIC ${SRC_FILES})

