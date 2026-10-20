# Interface translations, one set of files per module.
#
# Strings are English in the source (tr, qsTr) and translated in
# src/<module>/i18n/<name>_<language>.ts, where <name> is the module's path
# under src/ with "/" replaced by "_" (src/ui/settings -> ui_settings_ru.ts).
# Separate files per module keep branches from editing one shared XML file.
#
# jfd_add_translations(<target>) compiles the calling module's .ts files and
# embeds the .qm files in <target> as :/i18n/<name>_<language>.qm. The
# application loads every :/i18n/*_<language>.qm for the chosen language.
#
# After changing interface strings:
#   cmake --build <build> --target update_translations
# rescans each module's directory with lupdate and rewrites its .ts files;
# translate the new entries (Qt Linguist or by hand) and commit them.
#
# The translations_check test runs the same scan on copies of the .ts files
# and fails when a string is missing, untranslated, or no longer used.

set(JFD_LANGUAGES ru)

if(JFD_REQUIRE_QT OR JFD_BUILD_APP)
  find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
else()
  find_package(Qt6 QUIET COMPONENTS LinguistTools)
endif()

set_property(GLOBAL PROPERTY JFD_TRANSLATION_ENTRIES "")

function(jfd_add_translations target)
  if(NOT TARGET Qt6::lrelease)
    message(STATUS "Qt Linguist tools not found: ${target} is built without translations")
    return()
  endif()
  file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}")
  string(REPLACE "/" "_" name "${rel}")
  set(ts_files "")
  foreach(language IN LISTS JFD_LANGUAGES)
    set(ts "${CMAKE_CURRENT_SOURCE_DIR}/i18n/${name}_${language}.ts")
    if(NOT EXISTS "${ts}")
      # A new module starts with an empty file; update_translations fills it.
      file(WRITE "${ts}" "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<!DOCTYPE TS>\n<TS version=\"2.1\" language=\"${language}\">\n</TS>\n")
      message(STATUS "Created ${ts}")
    endif()
    list(APPEND ts_files "${ts}")
    set_property(GLOBAL APPEND PROPERTY JFD_TRANSLATION_ENTRIES "${CMAKE_CURRENT_SOURCE_DIR}|${ts}")
  endforeach()

  # lrelease is called directly: qt_add_lrelease changed its signature in Qt
  # 6.7 and this file serves both the pinned Qt and older distribution Qt.
  set(qm_dir "${CMAKE_CURRENT_BINARY_DIR}/i18n")
  set(qm_files "")
  foreach(ts IN LISTS ts_files)
    get_filename_component(qm_name "${ts}" NAME_WE)
    set(qm "${qm_dir}/${qm_name}.qm")
    add_custom_command(OUTPUT "${qm}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${qm_dir}"
      COMMAND Qt6::lrelease -silent "${ts}" -qm "${qm}"
      DEPENDS Qt6::lrelease "${ts}"
      VERBATIM)
    list(APPEND qm_files "${qm}")
  endforeach()
  qt_add_resources(${target} "${target}_i18n" PREFIX "/i18n" BASE "${qm_dir}" FILES ${qm_files})
endfunction()

# Called once at the end of the root CMakeLists.txt, after every module.
function(jfd_finish_translations)
  if(NOT TARGET Qt6::lupdate)
    return()
  endif()
  get_property(entries GLOBAL PROPERTY JFD_TRANSLATION_ENTRIES)
  set(list_file "${PROJECT_BINARY_DIR}/jfd-translations.cmake")
  file(WRITE "${list_file}" "set(JFD_TRANSLATION_ENTRIES \"${entries}\")\n")
  set(script "${PROJECT_SOURCE_DIR}/cmake/JfdLupdate.cmake")
  add_custom_target(update_translations
    COMMAND "${CMAKE_COMMAND}" "-DLUPDATE=$<TARGET_FILE:Qt6::lupdate>" "-DENTRIES_FILE=${list_file}"
      -DMODE=update -P "${script}"
    COMMENT "Updating the .ts files from the sources"
    VERBATIM)
  if(JFD_BUILD_TESTS)
    add_test(NAME translations_check
      COMMAND "${CMAKE_COMMAND}" "-DLUPDATE=$<TARGET_FILE:Qt6::lupdate>" "-DENTRIES_FILE=${list_file}"
        -DMODE=check "-DWORK_DIR=${PROJECT_BINARY_DIR}/translations-check" -P "${script}")
  endif()
endfunction()
