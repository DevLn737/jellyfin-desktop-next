# Script mode (cmake -P), run by the licenses_check test.
#   -DSOURCE_DIR=<repository root>
# Every file in a folder named icons* under src/ must have its own row in
# LICENSES.md: | `src/.../icons/name.svg` | source | licence |
# with a source and a licence filled in, and every such row must name a file
# that exists (no stale entries).
cmake_policy(VERSION 3.24)

file(GLOB_RECURSE all_files RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/src/*")
set(icons "")
foreach(path IN LISTS all_files)
  if(path MATCHES "/icons[^/]*/[^/]+$")
    list(APPEND icons "${path}")
  endif()
endforeach()

file(STRINGS "${SOURCE_DIR}/LICENSES.md" lines)
set(listed "")
set(problems "")
foreach(line IN LISTS lines)
  if(NOT line MATCHES "^\\|[ \t]*`(src/[^`]*/icons[^/`]*/[^`]+)`[ \t]*\\|")
    continue()
  endif()
  set(path "${CMAKE_MATCH_1}")
  if(NOT line MATCHES "^\\|[^|]+\\|[ \t]*[^ \t|][^|]*\\|[ \t]*[^ \t|][^|]*\\|[ \t]*$")
    string(APPEND problems "  LICENSES.md: the row of ${path} needs a source and a licence\n")
  endif()
  if(NOT EXISTS "${SOURCE_DIR}/${path}")
    string(APPEND problems "  LICENSES.md lists ${path}, which does not exist\n")
  endif()
  if(path IN_LIST listed)
    string(APPEND problems "  LICENSES.md lists ${path} twice\n")
  endif()
  list(APPEND listed "${path}")
endforeach()

foreach(path IN LISTS icons)
  if(NOT path IN_LIST listed)
    string(APPEND problems "  ${path} has no row in LICENSES.md\n")
  endif()
endforeach()

if(problems)
  message(FATAL_ERROR "Icon licences (LICENSES.md):\n${problems}")
endif()
list(LENGTH icons count)
message(STATUS "${count} icon files, each with its licence in LICENSES.md")
