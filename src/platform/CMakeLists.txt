# Operating system services: credential store, sleep inhibition, single
# instance and system media controls (SMTC on Windows, MPRIS on Linux).
add_library(jfd_platform STATIC
  src/CredentialStore.cpp
  src/SleepInhibitor.cpp
  src/SingleInstance.cpp
  src/MediaControls.cpp
  include/jfd/platform/CredentialStore.h
  include/jfd/platform/SleepInhibitor.h
  include/jfd/platform/SingleInstance.h
  include/jfd/platform/MediaControls.h)
add_library(jfd::platform ALIAS jfd_platform)
target_include_directories(jfd_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_platform PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(jfd_platform PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
set_target_properties(jfd_platform PROPERTIES AUTOMOC ON)

if(WIN32)
  find_package(Qt6 REQUIRED COMPONENTS Gui)
  target_sources(jfd_platform PRIVATE
    src/CredentialStoreWindows.cpp
    src/SleepInhibitorWindows.cpp
    src/MediaControlsWindows.cpp)
  # C++/WinRT for SMTC; WindowsApp provides the Windows Runtime activation.
  target_link_libraries(jfd_platform PRIVATE advapi32 WindowsApp Qt6::Gui)
  target_compile_options(jfd_platform PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/bigobj>)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
  endif()
  if(NOT APPLE AND LIBSECRET_FOUND)
    target_sources(jfd_platform PRIVATE src/CredentialStoreSecretService.cpp)
    target_link_libraries(jfd_platform PRIVATE PkgConfig::LIBSECRET)
  else()
    message(STATUS "No credential store for this platform: tokens are kept in memory only")
    target_sources(jfd_platform PRIVATE src/CredentialStoreNone.cpp)
  endif()

  find_package(Qt6 QUIET COMPONENTS DBus)
  if(NOT APPLE AND Qt6DBus_FOUND)
    # Desktop portal / ScreenSaver inhibition and MPRIS.
    target_sources(jfd_platform PRIVATE
      src/SleepInhibitorDBus.cpp
      src/MediaControlsMpris.cpp
      include/jfd/platform/DBusServices.h)
    target_link_libraries(jfd_platform PUBLIC Qt6::DBus)
    target_compile_definitions(jfd_platform PUBLIC JFD_PLATFORM_DBUS=1)
  else()
    message(STATUS "Qt D-Bus not found: no sleep inhibition or MPRIS")
    target_sources(jfd_platform PRIVATE src/PlatformNone.cpp)
  endif()
endif()
