# collect up the source files
file(GLOB SRC_FILES "*.c")

add_library(uulib STATIC ${SRC_FILES})

target_include_directories(uulib PRIVATE ".")
target_compile_definitions(uulib PUBLIC "PROTOTYPES")
