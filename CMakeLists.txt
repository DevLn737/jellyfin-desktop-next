cmake_minimum_required(VERSION 3.24)
project(JellyfinDesktopNext VERSION 0.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(JFD_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror)
endif()

add_subdirectory(src/core)

if(JFD_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
