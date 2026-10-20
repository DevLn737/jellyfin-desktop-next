# Qt and libmpv for the application targets. Versions are pinned in
# deps/versions.json; JFD_ENFORCE_PINS turns a mismatch into a configure error
# (CI sets it, local builds may use whatever Qt 6 and libmpv are installed).

option(JFD_ENFORCE_PINS "Fail when Qt or libmpv differ from deps/versions.json" OFF)

file(READ "${PROJECT_SOURCE_DIR}/deps/versions.json" _jfd_versions)
string(JSON JFD_PINNED_QT_VERSION GET "${_jfd_versions}" qt version)
string(JSON JFD_PINNED_LINUX_LIBMPV GET "${_jfd_versions}" linux libmpvVersion)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui OpenGL Quick Qml QuickControls2)
qt_standard_project_setup(REQUIRES 6.5)
if(JFD_ENFORCE_PINS AND NOT Qt6_VERSION VERSION_EQUAL JFD_PINNED_QT_VERSION)
  message(FATAL_ERROR "Qt ${Qt6_VERSION} found, ${JFD_PINNED_QT_VERSION} pinned")
endif()

# libmpv: on Windows the fetch script provides MPV_INCLUDE_DIR and MPV_LIBRARY
# (an MSVC import library generated from the pinned DLL); elsewhere pkg-config
# unless both are given explicitly.
if(WIN32 OR (MPV_INCLUDE_DIR AND MPV_LIBRARY))
  if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
    message(FATAL_ERROR "Set MPV_INCLUDE_DIR and MPV_LIBRARY (scripts/fetch-windows-deps.ps1)")
  endif()
  if(JFD_ENFORCE_PINS AND NOT WIN32)
    message(FATAL_ERROR "Pinned Linux builds take libmpv from pkg-config")
  endif()
  set(MPV_VERSION "from ${MPV_LIBRARY}")
  add_library(jfd_libmpv INTERFACE)
  target_include_directories(jfd_libmpv SYSTEM INTERFACE "${MPV_INCLUDE_DIR}")
  target_link_libraries(jfd_libmpv INTERFACE "${MPV_LIBRARY}")
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
  if(JFD_ENFORCE_PINS AND NOT MPV_VERSION VERSION_EQUAL JFD_PINNED_LINUX_LIBMPV)
    message(FATAL_ERROR "libmpv ${MPV_VERSION} found, ${JFD_PINNED_LINUX_LIBMPV} pinned")
  endif()
  add_library(jfd_libmpv INTERFACE)
  target_link_libraries(jfd_libmpv INTERFACE PkgConfig::MPV)
endif()
add_library(mpv::mpv ALIAS jfd_libmpv)

message(STATUS "Qt ${Qt6_VERSION}, libmpv ${MPV_VERSION}")
