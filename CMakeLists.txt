cmake_minimum_required(VERSION 3.24)
project(JellyfinDesktopNext VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JFD_BUILD_TESTS "Build unit tests" ON)
# The Qt-based modules are built when Qt 6 is found. CI sets JFD_REQUIRE_QT so
# a missing Qt fails the build instead of silently skipping them.
option(JFD_REQUIRE_QT "Fail when Qt 6 is not found" OFF)
# Tests against a live Jellyfin server (see scripts/test-server).
option(JFD_SERVER_TESTS "Build tests that need a Jellyfin server" OFF)
set(JFD_SIM_SCENARIOS 10000 CACHE STRING "Randomized simulator scenarios run by ctest")

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

# nlohmann/json 3.11.3 single header, vendored in third_party/nlohmann (MIT).
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/third_party/nlohmann)

add_subdirectory(src/core)
add_subdirectory(src/syncplay)

if(JFD_REQUIRE_QT)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Core Network WebSockets)
else()
  find_package(Qt6 6.4 QUIET COMPONENTS Core Network WebSockets)
endif()

if(Qt6_FOUND)
  add_subdirectory(src/platform)
  add_subdirectory(src/jellyfin-api)
  add_subdirectory(src/library)
else()
  message(STATUS "Qt 6 not found: building only the Qt-free core")
endif()

if(JFD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
