# see https://cmake.org/cmake/help/latest/module/FindGettext.html
find_package(Gettext)
file(STRINGS "${CMAKE_SOURCE_DIR}/po/LINGUAS" lang_list REGEX "^[a-z]")
foreach(lang IN LISTS lang_list)
  # this creates the gmo file in build directory instead of po/*.gmo. Although this is
  # a change from previous build system, it may not be a problem
  GETTEXT_PROCESS_PO_FILES(
    ${lang} ALL
    PO_FILES "${CMAKE_SOURCE_DIR}/po/${lang}.po"
  )
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${lang}.gmo
    DESTINATION "${CMAKE_INSTALL_LOCALEDIR}/${lang}/LC_MESSAGES/"
    RENAME pan.mo
  )
endforeach()
add_dependencies(pan pofiles)

