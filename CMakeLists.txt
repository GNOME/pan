cmake_minimum_required(VERSION 3.25)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "Do not build in-source. Please remove CMakeCache.txt and the CMakeFiles/ directory. Then build out-of-source.")
endif()

file(STRINGS "NEWS" news LIMIT_COUNT 1)
string(REGEX MATCH "[0-9.]+" pan_version ${news})
string(REGEX MATCH "\"[a-zA-Z ]+\"" quoted_pan_title ${news})
# remove quotes
string(REGEX REPLACE "\"" "" VERSION_TITLE ${quoted_pan_title})
message(STATUS "Building Pan version ${pan_version} (${VERSION_TITLE})")

# https://cmake.org/cmake/help/latest/command/project.html
project(Pan
  VERSION ${pan_version}
  DESCRIPTION "Pan usenet reader"
  HOMEPAGE_URL "https://gitlab.gnome.org/GNOME/pan"
)

# #cmakedefine completely refuses to recognise CMAKE_PROJECT_VERSION_xxxx or
# PROJECT_VERSION_xxxx
set(VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(VERSION_TWEAK ${PROJECT_VERSION_TWEAK})

set(PAN_DBUS_SERVICE_NAME      "org.gnome.pan")
set(PAN_DBUS_SERVICE_PATH      "/news/pan/NZB")

# Standard CMake modules
include(CTest)

# https://cmake.org/cmake/help/latest/module/CheckIncludeFile.html
include(CheckIncludeFile)
# This will define the default values for installation directories (all platforms even if named GNU)
# https://cmake.org/cmake/help/latest/module/GNUInstallDirs.html
include(GNUInstallDirs)
set(PACKAGE "pan")

set(PACKAGE_BUGREPORT "https://gitlab.gnome.org/GNOME/pan/issues")

# see https://gitlab.gnome.org/GNOME/pan/-/issues/175
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# see https://cmake.org/cmake/help/latest/module/FindThreads.html
find_package(Threads REQUIRED)

# backward compat. There's a lot of ifdef #HAVE_CONFIG_H, #include config.h
# Don't know why...
# https://cmake.org/cmake/help/latest/command/add_compile_definitions.html
add_compile_definitions(HAVE_CONFIG_H)

# Silence deprecation warnings for things deprecated before 2.26,
# and generally set the target behaviour to be like GLib 2.26.
# In principle this should match $GLIB_REQUIRED, but 2.26 is the oldest
# version for which the macros exist (and has been unsupported since
# around 2011 in any case).
add_compile_definitions(GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_26)

# Highest version number from which new features are allowed to be used,
# even within a GLIB_CHECK_VERSION check. GLib will produce compile-time
# warnings when features of a newer GLib version are used.
add_compile_definitions("GLIB_VERSION_MAX_ALLOWED=G_ENCODE_VERSION(2,36)")

include_directories("${CMAKE_SOURCE_DIR}")
# this is where config.h is generated
include_directories(${CMAKE_BINARY_DIR})

# https://cmake.org/cmake/help/latest/command/add_executable.html
add_executable(pan pan/gui/pan.cc pan/gui/gui.cc)
# remove this so pan binary lands in build dir. But this requires
# first to rename pan/ dir to src/
set_target_properties(pan PROPERTIES RUNTIME_OUTPUT_DIRECTORY "pan/gui")
target_compile_options(pan PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")

# Don't embed GIT_REV for a reproducible build or when building from a
# tarball. The former is mostly done in Linux distribution build where
# git rev is not the same as upstream.
if (EXISTS ".git" AND NOT DEFINED ENV{SOURCE_DATE_EPOCH})
  # Get the latest abbreviated commit hash of the working branch
  # see https://jonathanhamberg.com/post/cmake-embedding-git-hash/
  # drawback: commited changed are not taken into account until "cmake ." is run
  execute_process(
    COMMAND git log -1 --format=%h
    WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
    OUTPUT_VARIABLE GIT_REV
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message(STATUS "Git rev: ${GIT_REV}")
endif ()

CHECK_INCLUDE_FILE(errno.h HAVE_ERRNO_H)
CHECK_INCLUDE_FILE(fcntl.h HAVE_FCNTL_H)
CHECK_INCLUDE_FILE(inttypes.h HAVE_INTTYPES_H)
CHECK_INCLUDE_FILE(unistd.h HAVE_UNISTD_H)

# https://cmake.org/cmake/help/latest/module/CheckSymbolExists.html
include(CheckSymbolExists)
check_symbol_exists(close "unistd.h" HAVE_CLOSE)
check_symbol_exists(localtime_r "time.h" HAVE_LOCALTIME_R)

set(GETTEXT_PACKAGE pan)

find_package(PkgConfig REQUIRED)

function(my_target_system_lib target scope libname)
  # https://cmake.org/cmake/help/latest/command/include_directories.html
  target_include_directories(${target} ${scope} ${${libname}_INCLUDE_DIRS})
  target_compile_options(${target} ${scope} ${${libname}_CFLAGS})
  target_link_directories(${target} ${scope} ${${libname}_LIBRARY_DIRS})
  target_link_libraries(${target} ${scope} ${${libname}_LIBRARIES})
endfunction()

# see https://github.com/shlomif/gtk3-cmake-examples/blob/master/CMakeLists.txt
# Use the package PkgConfig to detect GTK+ headers/library files
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET  gtk+-3.0>=3.16.0)

# https://cmake.org/cmake/help/latest/command/find_package.html
# https://cmake.org/cmake/help/latest/module/FindIconv.html
find_package(Iconv)
set(HAVE_ICONV ${Iconv_FOUND})

# https://cmake.org/cmake/help/latest/module/FindZLIB.html
find_package(ZLIB 1.2.0 REQUIRED)
set(HAVE_ZLIB ${ZLIB_FOUND})

# D-Bus support
# https://cmake.org/cmake/help/latest/command/option.html
option(WANT_DBUS "enable D-Bus support (default: no)")
if(WANT_DBUS)
  set(HAVE_DBUS TRUE)
endif()

# https://cmake.org/cmake/help/latest/module/FindPkgConfig.html#command:pkg_check_modules
pkg_check_modules(GMIME REQUIRED IMPORTED_TARGET gmime-3.0)

pkg_check_modules(GLIB REQUIRED IMPORTED_TARGET glib-2.0>=2.26.0)

# Check for GMime cryptography support
option(WANT_GMIME_CRYPTO "enable GMime cryptography support (default: no)")
if(WANT_GMIME_CRYPTO)
  set(HAVE_GMIME_CRYPTO TRUE)
endif()

# GtkSpell support
option(WANT_GSPELL "enable Gspell support (default: on)" ON)
if(WANT_GSPELL)
  pkg_check_modules(GSPELL REQUIRED IMPORTED_TARGET gspell-1)
  set(HAVE_GSPELL true)
endif()

option(WANT_WEBKIT "enable WebKitGTK support (experimental, default: off)")
if(WANT_WEBKIT)
  pkg_check_modules(WEBKITGTK REQUIRED IMPORTED_TARGET webkitgtk-3.0>=1.8.1)
  set(HAVE_WEBKIT true)
endif()

option(WANT_GNUTLS "enable GnuTLS support (default: on)" ON)
if(WANT_GNUTLS)
  pkg_check_modules(GNUTLS REQUIRED IMPORTED_TARGET gnutls>=3.3.6)
  set(HAVE_GNUTLS true)
endif()

option(WANT_NOTIFY "enable libnotify support (default: off)")
if(WANT_NOTIFY)
  pkg_check_modules(LIBNOTIFY REQUIRED IMPORTED_TARGET libnotify>=0.4.1)
  set(HAVE_LIBNOTIFY true)
endif()

option(WANT_GKR "enable GNOME Keyring or libsecret support (default: off)")
if(WANT_GKR)
  pkg_check_modules(LIBSECRET REQUIRED IMPORTED_TARGET libsecret-1>=0.16)
  pkg_check_modules(GCR3 REQUIRED IMPORTED_TARGET gcr-3>=3.20)
  set(HAVE_GKR TRUE)
endif()

option(ENABLE_MANUAL "built in user manual (default: off)")
if (ENABLE_MANUAL)
  set(HAVE_MANUAL 1)
endif()

# Check to see if strftime supports the use of %l and %k
# https://cmake.org/cmake/help/latest/command/try_run.html
# this requires cmake 3.25
try_run(HAVE_LKSTRFTIME COMPILED_LKSTRFTIME_TEST
  SOURCE_FROM_CONTENT test-strftime.c "
#include <string.h>
#include <stdlib.h>
#include <time.h>
int main(int argc, char **argv) {
  char buf[10];
  time_t rawtime = time(0);
  struct tm *timeinfo = localtime (&rawtime);
  strftime(buf, 10, \"%l %k\", timeinfo);
  exit (strstr(buf, \"l\") || strstr(buf, \"k\") || !strcmp(buf,\" \") || !strlen(buf));
}
")

if(NOT COMPILED_LKSTRFTIME_TEST)
  message(WARNING "failed to compile test-strftime.c")
endif()

# Don't embed uname -sr in PLATFORM_INFO for a reproducible build.
if (NOT DEFINED ENV{SOURCE_DATE_EPOCH})
  set(PLATFORM_INFO ${CMAKE_HOST_SYSTEM})
  message(STATUS "platform: ${PLATFORM_INFO}")
endif ()

# https://cmake.org/cmake/help/latest/manual/cmake-variables.7.html
if(APPLE)
  set(G_OS_DARWIN TRUE)
  # TODO: set LOCALEDIR
elseif(WIN32)
  # https://cmake.org/cmake/help/latest/command/target_compile_options.html
  #add_compile_options(mms-bitfields)
  add_compile_definitions(WIN32_LEAN_AND_MEAN)
  target_link_libraries(pan shell32 ws2_32 pthread)
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mwindows")
  # TODO: set LOCALEDIR
else()
  set(LOCALEDIR "/usr/share/locale")
endif()

# https://cmake.org/cmake/help/latest/command/add_subdirectory.html
add_subdirectory(uulib)
add_subdirectory(pan)
add_subdirectory(po)
add_subdirectory(help)

# https://cmake.org/cmake/help/latest/command/configure_file.html
configure_file(${CMAKE_SOURCE_DIR}/config-cmake.h.in ${CMAKE_BINARY_DIR}/config.h)

target_compile_definitions(pan PRIVATE "PANLOCALEDIR=\"${LOCALEDIR}\"")

# https://cmake.org/cmake/help/latest/command/target_link_libraries.html
target_link_libraries(pan
  pan-cc-gui
  pan-c-gui
  data-impl
  tasks
  data
  usenet-utils
  generalutils
  uulib
  ${CMAKE_THREAD_LIBS_INIT}
  PkgConfig::GTK3
  PkgConfig::GMIME
)

if(WIN32)
  target_link_libraries(pan ws2_32)
endif()

# Installation
# https://cmake.org/cmake/help/book/mastering-cmake/chapter/Install.html
# https://cmake.org/cmake/help/latest/command/install.html
install(TARGETS pan DESTINATION ${CMAKE_INSTALL_BINDIR})

set(meta "org.gnome.pan.metainfo.xml")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${meta}.in" "${CMAKE_CURRENT_BINARY_DIR}/${meta}")
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${meta}"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
)

set(desktop "org.gnome.pan.desktop")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${desktop}.in" "${CMAKE_CURRENT_BINARY_DIR}/${desktop}")
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${desktop}"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
)

set(dbus-file "org.gnome.pan.service")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${dbus-file}.in" "${CMAKE_CURRENT_BINARY_DIR}/${dbus-file}")
install(
  FILES "${CMAKE_CURRENT_BINARY_DIR}/${dbus-file}"
  DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/dbus-1/services"
)

# install pan man page
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/man1")
file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/pan.1" "${CMAKE_CURRENT_BINARY_DIR}/man1/pan.1")
install(DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/man1" TYPE MAN)
