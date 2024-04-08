# need to build docbook
# see https://build.opensuse.org/package/live_build_log/openSUSE:Factory/pan/standard/x86_64

if (ENABLE_MANUAL)

  # https://cmake.org/cmake/help/latest/command/find_program.html
  find_program(has_itstool NAMES itstool REQUIRED)
  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/C/figures")
  set(png_path "figures/pan_window.png")
  file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/C/${png_path}" "${CMAKE_CURRENT_BINARY_DIR}/C/${png_path}")
  file(COPY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/C/index.docbook" "${CMAKE_CURRENT_BINARY_DIR}/C/index.docbook")

  set(lang_list cs de es sv)
  foreach(lang IN LISTS lang_list)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${lang}/figures")
    add_custom_target(
      "${lang}-docbook" ALL
      BYPRODUCTS "${lang}/${lang}.mo"
      DEPENDS "${lang}/${lang}.po"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/${lang}"
      COMMAND msgfmt -o "${lang}.mo" "${CMAKE_CURRENT_SOURCE_DIR}/${lang}/${lang}.po"
      COMMAND itstool -m "${lang}.mo" "${CMAKE_CURRENT_SOURCE_DIR}/C/index.docbook"
    )
    file(CREATE_LINK "../../../C/pan/${png_path}" "${CMAKE_CURRENT_BINARY_DIR}/${lang}/${png_path}" SYMBOLIC)
  endforeach()

  foreach(lang IN LISTS lang_list ITEMS C)
    # https://cmake.org/cmake/help/latest/command/install.html#files
    install(
      FILES "${CMAKE_CURRENT_BINARY_DIR}/${lang}/index.docbook"
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/help/${lang}/pan/"
    )
    install(
      FILES    "${CMAKE_CURRENT_BINARY_DIR}/${lang}/${png_path}"
      DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/help/${lang}/pan/figures"
    )
  endforeach()

  install(FILES "C/legal.xml" DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/help/C/pan/")

endif()
