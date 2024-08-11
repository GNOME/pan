set(SRC_CC_FILES
 actions.cc
 body-pane.cc
 dl-headers-ui.cc
 editor-spawner.cc
 group-pane.cc
 group-prefs-dialog.cc
 group-prefs.cc
 header-pane.cc
 hig.cc
 load-icon.cc
 log-ui.cc
 pan-file-entry.cc
 pan-tree.cc
 post-ui.cc
 prefs-file.cc
 prefs-ui.cc
 prefs.cc
 profiles-dialog.cc
 progress-view.cc
 render-bytes.cc
 save-attach-ui.cc
 save-ui.cc
 score-add-ui.cc
 score-view-ui.cc
 server-ui.cc
 task-pane.cc
 url.cc
)

set(SRC_C_FILES
 e-action-combo-box.c
 e-charset-combo-box.c
 e-charset-dialog.c
 e-charset.c
 e-cte-dialog.c
 xface.c
)

# https://cmake.org/cmake/help/latest/command/add_library.html
add_library(pan-cc-gui STATIC ${SRC_CC_FILES})

add_library(pan-c-gui STATIC ${SRC_C_FILES})

foreach (my_target "pan-cc-gui" "pan-c-gui")
  target_link_libraries(${my_target} PRIVATE tasks)

  target_link_libraries(${my_target} PUBLIC PkgConfig::GTK3)
  target_link_libraries(${my_target} PUBLIC PkgConfig::GMIME)

  if(WANT_GSPELL)
    my_target_system_lib(${my_target} PUBLIC GSPELL)
  endif()

  if(WANT_WEBKIT)
    target_link_libraries(${my_target} PUBLIC PkgConfig::WEBKITGTK)
  endif()

  if(WANT_GNUTLS)
    target_link_libraries(${my_target} PUBLIC PkgConfig::GNUTLS)
  endif()

  if(WANT_NOTIFY)
    target_link_libraries(${my_target} PUBLIC PkgConfig::LIBNOTIFY)
  endif()

  if(Iconv_FOUND)
    my_target_system_lib(${my_target} PUBLIC Iconv)
  endif()

  if(WANT_GKR)
    target_link_libraries(${my_target} PUBLIC PkgConfig::LIBSECRET)
    target_link_libraries(${my_target} PUBLIC PkgConfig::GCR3)
  endif()

  if(WANT_WEBKIT)
    target_link_libraries(${my_target} PUBLIC PkgConfig::WEBKITGTK)
  endif()

endforeach()

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(pan-cc-gui PRIVATE "-Wreorder" "${CXX_STD}"  "-Wzero-as-null-pointer-constant")

if(WIN32)
  target_sources(pan PRIVATE panrc.rc)
endif()
