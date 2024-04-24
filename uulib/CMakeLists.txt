# collect up the source files
file(GLOB SRC_FILES "*.c")

add_library(uulib STATIC ${SRC_FILES})

target_include_directories(uulib PRIVATE ".")
target_compile_definitions(uulib PUBLIC "PROTOTYPES")

target_link_libraries(uulib PUBLIC PkgConfig::GLIB)

# see https://cmake.org/cmake/help/latest/module/FindZLIB.html
if(ZLIB_FOUND)
    target_include_directories(uulib PUBLIC ${ZLIB_INCLUDE_DIRS})
    target_link_libraries(uulib PUBLIC ${ZLIB_LIBRARIES})
endif()
