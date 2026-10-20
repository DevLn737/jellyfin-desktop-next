# Modules of the application, one directory each under src/.
#
# jfd_add_module(<dir>) adds <dir> when it has a CMakeLists.txt and does
# nothing otherwise. The root CMakeLists.txt lists every planned module this
# way, so a new module joins the build by creating its own CMakeLists.txt; the
# root file and cmake/ stay unchanged.
function(jfd_add_module dir)
  if(EXISTS "${PROJECT_SOURCE_DIR}/${dir}/CMakeLists.txt")
    add_subdirectory("${PROJECT_SOURCE_DIR}/${dir}" "${PROJECT_BINARY_DIR}/${dir}")
  endif()
endfunction()
