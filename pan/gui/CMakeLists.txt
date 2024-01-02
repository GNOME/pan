file(GLOB SRC_C_FILES "*.c")
set(SRC_CC_FILES 
 actions.cc
 body-pane.cc
 dl-headers-ui.cc
 e-action-combo-box.c
 e-charset.c
 e-charset-combo-box.c
 e-charset-dialog.c
 e-cte-dialog.c
 group-pane.cc
 group-prefs.cc
 group-prefs-dialog.cc
 header-pane.cc
 hig.cc
 log-ui.cc
 pan-file-entry.cc
 pan-pixbufs.c
 pan-tree.cc
 post-ui.cc
 prefs.cc
 prefs-file.cc
 prefs-ui.cc
 progress-view.cc
 profiles-dialog.cc
 render-bytes.cc
 save-ui.cc
 save-attach-ui.cc
 score-add-ui.cc
 score-view-ui.cc
 server-ui.cc
 task-pane.cc
 xface.c
 url.cc
)

add_library(pangui STATIC ${SRC_C_FILES} ${SRC_CC_FILES})

