# doctest 2.4.11 single header, vendored in third_party/doctest (MIT).
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/third_party/doctest)

add_executable(jfd_core_tests
  core/main.cpp
  core/TimeTest.cpp
  core/EpochTest.cpp
  core/InterfacesTest.cpp)
target_link_libraries(jfd_core_tests PRIVATE jfd::core doctest)
add_test(NAME jfd_core_tests COMMAND jfd_core_tests)

add_executable(jfd_diagnostics_tests
  core/main.cpp
  diagnostics/FrameStatsTest.cpp
  diagnostics/ProcessStatsTest.cpp)
target_link_libraries(jfd_diagnostics_tests PRIVATE jfd::diagnostics doctest)
add_test(NAME jfd_diagnostics_tests COMMAND jfd_diagnostics_tests)

if(JFD_BUILD_APP)
  add_subdirectory(render)
endif()
