# collect up the source files
file(GLOB SRC_FILES "*.c")

add_library(uulib STATIC ${SRC_FILES})

