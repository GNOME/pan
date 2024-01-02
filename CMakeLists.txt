cmake_minimum_required(VERSION 3.20)
project(Pan)

# see https://github.com/shlomif/gtk3-cmake-examples/blob/master/CMakeLists.txt
# Use the package PkgConfig to detect GTK+ headers/library files
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
# Setup CMake to use GTK+, tell the compiler where to look for headers
# and to the linker where to look for libraries
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
# Add other flags to the compiler
add_definitions(${GTK3_CFLAGS_OTHER})

add_subdirectory(uulib)
add_subdirectory(pan)

add_executable(pan pan/gui/pan.cc)

target_link_libraries(pan data pangui generalutils tasks uulib ${GTK_LIBRARIES})

