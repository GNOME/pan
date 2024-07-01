# order of add_subdirectory is important
foreach(subdir data data-migration data-impl general icons tasks usenet-utils gui)
  add_subdirectory(${subdir})
endforeach()
