# Script mode (cmake -P), run by the qml_literals_check test.
#   -DSOURCE_DIR=<repository root>
# Fails on a QML property that people read (text, title, labels, tooltips,
# accessible names) set to a plain string literal instead of qsTr(...): such
# a string never reaches the .ts files, so translations_check cannot see it.
# Allowed: the product name "Jellyfin", addresses (they contain "://"), and a
# line that ends with "// untranslated: <why>".

file(GLOB_RECURSE qml_files RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/src/*.qml")
set(properties "text|title|placeholderText|label|description|subtitle|message|ToolTip\\.text|Accessible\\.name|Accessible\\.description")
set(problems "")
foreach(path IN LISTS qml_files)
  file(STRINGS "${SOURCE_DIR}/${path}" lines)
  set(number 0)
  foreach(line IN LISTS lines)
    math(EXPR number "${number} + 1")
    if(NOT line MATCHES "^[ \t]*(${properties})[ \t]*:[ \t]*\"([^\"]*)\"")
      continue()
    endif()
    set(value "${CMAKE_MATCH_2}")
    if(NOT value MATCHES "[A-Za-z]" OR value STREQUAL "Jellyfin" OR value MATCHES "://"
       OR line MATCHES "// untranslated: ")
      continue()
    endif()
    string(APPEND problems "  ${path}:${number}: \"${value}\"\n")
  endforeach()
endforeach()

if(problems)
  message(FATAL_ERROR "Strings shown to people must go through qsTr():\n${problems}")
endif()
list(LENGTH qml_files count)
message(STATUS "${count} QML files: no untranslated literals")
