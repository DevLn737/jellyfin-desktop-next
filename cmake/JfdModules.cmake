# Modules of the application, one directory each under src/.
#
# jfd_add_module(<dir>) adds <dir> when it has a CMakeLists.txt and does
# nothing otherwise, or when the directory was already added with
# add_subdirectory. The root CMakeLists.txt lists every planned module this
# way, so a new module joins the build by creating its own CMakeLists.txt; the
# root file and cmake/ stay unchanged.
function(jfd_add_module dir)
  set(source "${PROJECT_SOURCE_DIR}/${dir}")
  if(NOT EXISTS "${source}/CMakeLists.txt")
    return()
  endif()
  get_directory_property(added DIRECTORY "${PROJECT_SOURCE_DIR}" SUBDIRECTORIES)
  if(source IN_LIST added)
    return()
  endif()
  add_subdirectory("${source}" "${PROJECT_BINARY_DIR}/${dir}")
endfunction()
