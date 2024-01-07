set(SRC_FILES 
 actions.cc
 body-pane.cc
 dl-headers-ui.cc
 e-action-combo-box.c
 e-charset-combo-box.c
 e-charset-dialog.c
 e-charset.c
 e-cte-dialog.c
 group-pane.cc
 group-prefs-dialog.cc
 group-prefs.cc
 header-pane.cc
 hig.cc
 log-ui.cc
 pan-file-entry.cc
 pan-pixbufs.c
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
 xface.c
)

# https://cmake.org/cmake/help/latest/command/add_library.html
add_library(pangui STATIC ${SRC_FILES})
add_dependencies(pangui icons_h)
