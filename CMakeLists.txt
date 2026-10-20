cmake_minimum_required(VERSION 3.24)
project(JellyfinDesktopNext VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JFD_BUILD_TESTS "Build unit tests" ON)
option(JFD_BUILD_APP "Build the Qt application (needs Qt 6 and libmpv)" OFF)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

if(JFD_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(src/core)
add_subdirectory(src/diagnostics)

if(JFD_BUILD_APP)
  include(cmake/JfdDependencies.cmake)
  add_subdirectory(src/media-backend)
  add_subdirectory(src/app)
endif()

if(JFD_BUILD_TESTS)
  add_subdirectory(tests)
endif()
