# SDL3 for gamepad input (src/platform, GamepadInput). Only the joystick,
# gamepad and HIDAPI subsystems are built: no video, audio, rendering or GPU.
# The pinned release archive (deps/versions.json, "sdl3") is downloaded and
# checked against its SHA256 at configure time, then built as a static
# library with the rest of the project, the same way on Windows (MSVC) and
# Linux. The packages need no SDL library of their own: it is inside the
# executable. On Linux SDL loads libudev at run time (dlopen) and works
# without it, so libudev stays the system's.
#
# Offline builds: -DFETCHCONTENT_SOURCE_DIR_SDL3=<unpacked SDL3-x.y.z>.
#
# Defines jfd_sdl3 (an interface target with SDL's headers as system headers)
# and JFD_SDL3_VERSION.

include(FetchContent)

file(READ "${PROJECT_SOURCE_DIR}/deps/versions.json" _jfd_sdl_pins)
string(JSON JFD_SDL3_VERSION GET "${_jfd_sdl_pins}" sdl3 version)
string(JSON _jfd_sdl_url GET "${_jfd_sdl_pins}" sdl3 url)
string(JSON _jfd_sdl_sha256 GET "${_jfd_sdl_pins}" sdl3 sha256)

FetchContent_Declare(SDL3
  URL "${_jfd_sdl_url}"
  URL_HASH SHA256=${_jfd_sdl_sha256}
  DOWNLOAD_EXTRACT_TIMESTAMP ON)

# SDL's own options, set before its CMakeLists.txt reads them.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_UNINSTALL OFF CACHE BOOL "" FORCE)
# The subsystems: joystick and HIDAPI (gamepad drivers for Xbox, Switch,
# PlayStation and 8BitDo controllers; the virtual joystick the tests use
# depends on it). Everything else off.
foreach(subsystem IN ITEMS AUDIO VIDEO GPU RENDER CAMERA HAPTIC POWER SENSOR DIALOG TRAY)
  set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
set(SDL_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI ON CACHE BOOL "" FORCE)
set(SDL_HIDAPI_JOYSTICK ON CACHE BOOL "" FORCE)
set(SDL_VIRTUAL_JOYSTICK ON CACHE BOOL "" FORCE)
# Linux: hidraw and evdev through udev (loaded at run time); no libusb,
# D-Bus, IBus, io_uring or PipeWire.
set(SDL_HIDAPI_LIBUSB OFF CACHE BOOL "" FORCE)
set(SDL_LIBUDEV ON CACHE BOOL "" FORCE)
set(SDL_DBUS OFF CACHE BOOL "" FORCE)
set(SDL_IBUS OFF CACHE BOOL "" FORCE)
set(SDL_LIBURING OFF CACHE BOOL "" FORCE)
set(SDL_PIPEWIRE OFF CACHE BOOL "" FORCE)
set(SDL_OFFSCREEN OFF CACHE BOOL "" FORCE)
set(SDL_RPATH OFF CACHE BOOL "" FORCE)
# No windows: SDL would otherwise insist on X11 or Wayland headers.
set(SDL_UNIX_CONSOLE_BUILD ON CACHE BOOL "" FORCE)
# Sanitizer builds instrument our C++ (CMAKE_CXX_FLAGS); SDL is C and is
# linked uninstrumented, like Qt and libmpv.
set(SDL_ASAN OFF CACHE BOOL "" FORCE)

enable_language(C)
# The project's warnings-as-errors are for our code; SDL is built with its
# own flags, as it is released.
get_directory_property(_jfd_sdl_saved_options COMPILE_OPTIONS)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "")
FetchContent_MakeAvailable(SDL3)
set_directory_properties(PROPERTIES COMPILE_OPTIONS "${_jfd_sdl_saved_options}")

if(NOT TARGET SDL3::SDL3-static)
  message(FATAL_ERROR "SDL3 ${JFD_SDL3_VERSION} did not define SDL3::SDL3-static")
endif()

# A source directory given by hand (FETCHCONTENT_SOURCE_DIR_SDL3) skips the
# hash check; its version must still be the pinned one.
file(STRINGS "${sdl3_SOURCE_DIR}/include/SDL3/SDL_version.h" _jfd_sdl_version_lines
  REGEX "^#define SDL_(MAJOR|MINOR|MICRO)_VERSION +[0-9]+")
set(_jfd_sdl_found "")
foreach(part IN ITEMS MAJOR MINOR MICRO)
  string(REGEX MATCH "SDL_${part}_VERSION +([0-9]+)" _match "${_jfd_sdl_version_lines}")
  list(APPEND _jfd_sdl_found "${CMAKE_MATCH_1}")
endforeach()
list(JOIN _jfd_sdl_found "." _jfd_sdl_found)
if(NOT _jfd_sdl_found VERSION_EQUAL JFD_SDL3_VERSION)
  message(FATAL_ERROR "SDL ${_jfd_sdl_found} found in ${sdl3_SOURCE_DIR}, ${JFD_SDL3_VERSION} pinned")
endif()

# Our code includes <SDL3/...> as system headers, so -Werror and /WX do not
# apply to them.
add_library(jfd_sdl3 INTERFACE)
target_include_directories(jfd_sdl3 SYSTEM INTERFACE "${sdl3_SOURCE_DIR}/include")
target_link_libraries(jfd_sdl3 INTERFACE SDL3::SDL3-static)
message(STATUS "SDL ${JFD_SDL3_VERSION} (static, gamepad input only)")
