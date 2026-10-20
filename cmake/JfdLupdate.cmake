# Script mode (cmake -P), used by cmake/JfdTranslations.cmake.
#   -DLUPDATE=<lupdate> -DENTRIES_FILE=<list> -DMODE=update|check [-DWORK_DIR=<dir>]
# update: rescans each module directory and rewrites its .ts file, dropping
#         strings the sources no longer use.
# check:  rescans into copies and fails on any string that is new (missing
#         from the committed file), untranslated, or no longer used.

include("${ENTRIES_FILE}")

set(problems "")
foreach(entry IN LISTS JFD_TRANSLATION_ENTRIES)
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 source_dir)
  list(GET parts 1 ts)
  if(MODE STREQUAL "update")
    execute_process(
      COMMAND "${LUPDATE}" -silent -locations none -no-obsolete "${source_dir}" -ts "${ts}"
      RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
      message(FATAL_ERROR "lupdate failed for ${ts}")
    endif()
    message(STATUS "Updated ${ts}")
    continue()
  endif()

  get_filename_component(name "${ts}" NAME)
  set(copy "${WORK_DIR}/${name}")
  file(MAKE_DIRECTORY "${WORK_DIR}")
  file(COPY_FILE "${ts}" "${copy}")
  execute_process(
    COMMAND "${LUPDATE}" -silent -locations none "${source_dir}" -ts "${copy}"
    RESULT_VARIABLE result)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "lupdate failed for ${ts}")
  endif()

  file(READ "${copy}" content)
  # One list element per message; ';' inside strings would split them.
  string(REPLACE ";" "@JFD_SEMICOLON@" content "${content}")
  string(REPLACE "</message>" ";" messages "${content}")
  foreach(message IN LISTS messages)
    if(message MATCHES "type=\"(unfinished|vanished|obsolete)\"")
      set(kind "${CMAKE_MATCH_1}")
      if(kind STREQUAL "unfinished")
        set(kind "untranslated or new")
      else()
        set(kind "no longer used")
      endif()
      string(REGEX MATCH "<source>([^<]*)</source>" _ "${message}")
      string(REPLACE "@JFD_SEMICOLON@" ";" source "${CMAKE_MATCH_1}")
      string(APPEND problems "  ${name}: ${kind}: \"${source}\"\n")
    endif()
  endforeach()
endforeach()

if(problems)
  message(FATAL_ERROR "Translations are out of date:\n${problems}"
    "Run: cmake --build <build> --target update_translations, translate, and commit the .ts files.")
endif()
list(LENGTH JFD_TRANSLATION_ENTRIES count)
if(MODE STREQUAL "check")
  message(STATUS "${count} translation files are complete")
endif()
