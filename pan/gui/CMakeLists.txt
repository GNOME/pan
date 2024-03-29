set(SRC_CC_FILES
 actions.cc
 body-pane.cc
 dl-headers-ui.cc
 group-pane.cc
 group-prefs-dialog.cc
 group-prefs.cc
 header-pane.cc
 hig.cc
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
 pan-pixbufs.c
 xface.c
)

# https://cmake.org/cmake/help/latest/command/add_library.html
add_library(pan-cc-gui STATIC ${SRC_CC_FILES})
add_dependencies(pan-cc-gui icons_h)

add_library(pan-c-gui STATIC ${SRC_C_FILES})

# Sanity checking
# https://cmake.org/cmake/help/latest/command/add_compile_options.html
target_compile_options(pan-cc-gui PRIVATE "${CXX_STD}" "-Wreorder" "-Wzero-as-null-pointer-constant")

if(WIN32)
  target_sources(pan PRIVATE panrc.rc)
endif()
