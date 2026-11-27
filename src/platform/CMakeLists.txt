# Operating system services: credential store, sleep inhibition, single
# instance, system media controls (SMTC on Windows, MPRIS on Linux), gamepad
# input (SDL3) and display modes (refresh rate switching).
add_library(jfd_platform STATIC
  src/CredentialStore.cpp
  src/AsyncCredentialStore.cpp
  src/SleepInhibitor.cpp
  src/SingleInstance.cpp
  src/MediaControls.cpp
  src/GamepadMapper.cpp
  src/DisplayModes.cpp
  src/DisplayModeSwitcher.cpp
  include/jfd/platform/CredentialStore.h
  include/jfd/platform/AsyncCredentialStore.h
  include/jfd/platform/SleepInhibitor.h
  include/jfd/platform/SingleInstance.h
  include/jfd/platform/MediaControls.h
  include/jfd/platform/InputAction.h
  include/jfd/platform/GamepadMapper.h
  include/jfd/platform/GamepadInput.h
  include/jfd/platform/DisplayModes.h
  include/jfd/platform/DisplayModeSwitcher.h)
add_library(jfd::platform ALIAS jfd_platform)
target_include_directories(jfd_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# AsyncCredentialStore's worker thread.
find_package(Threads REQUIRED)
target_link_libraries(jfd_platform PUBLIC Qt6::Core Qt6::Network PRIVATE Threads::Threads)
target_compile_definitions(jfd_platform PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
set_target_properties(jfd_platform PROPERTIES AUTOMOC ON)

if(JFD_GAMEPAD)
  include(${PROJECT_SOURCE_DIR}/cmake/JfdSdl3.cmake)
  target_sources(jfd_platform PRIVATE src/GamepadInputSdl.cpp)
  target_link_libraries(jfd_platform PRIVATE jfd_sdl3)
  # Tests drive SDL's virtual joystick.
  target_compile_definitions(jfd_platform PUBLIC JFD_PLATFORM_GAMEPAD=1)
else()
  message(STATUS "Gamepad input off (JFD_GAMEPAD)")
  target_sources(jfd_platform PRIVATE src/GamepadInputNone.cpp)
endif()

if(WIN32)
  find_package(Qt6 REQUIRED COMPONENTS Gui)
  target_sources(jfd_platform PRIVATE
    src/CredentialStoreWindows.cpp
    src/SleepInhibitorWindows.cpp
    src/MediaControlsWindows.cpp
    src/DisplayModesWindows.cpp)
  # C++/WinRT for SMTC; WindowsApp provides the Windows Runtime activation;
  # user32 has the display configuration calls.
  target_link_libraries(jfd_platform PRIVATE advapi32 user32 WindowsApp Qt6::Gui)
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

  # Display modes on X11 through Xlib and XRandR (libx11-dev, libxrandr-dev,
  # in packaging/linux/build-deps.txt; libmpv links them too, so the
  # AppImage already carries libXrandr). Without them the build still works
  # and the refresh rate is never switched.
  if(PkgConfig_FOUND)
    pkg_check_modules(JFD_XRANDR IMPORTED_TARGET GLOBAL x11 xrandr)
  endif()
  if(NOT APPLE AND JFD_XRANDR_FOUND)
    target_sources(jfd_platform PRIVATE src/DisplayModesX11.cpp)
    target_link_libraries(jfd_platform PRIVATE PkgConfig::JFD_XRANDR)
    target_compile_definitions(jfd_platform PUBLIC JFD_PLATFORM_XRANDR=1)
  elseif(NOT APPLE AND JFD_ENFORCE_PINS)
    # The packaged build switches the refresh rate on X11.
    message(FATAL_ERROR "Display modes need Xlib and XRandR (libx11-dev, libxrandr-dev)")
  else()
    message(STATUS "Xlib or XRandR not found: the display refresh rate is not switched")
    target_sources(jfd_platform PRIVATE src/DisplayModesNone.cpp)
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
