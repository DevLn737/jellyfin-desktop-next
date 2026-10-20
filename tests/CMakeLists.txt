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

if(TARGET jfd_jellyfin_api)
  find_package(Qt6 REQUIRED COMPONENTS Test)

  # Qt modules are tested with doctest under a QCoreApplication; network tests
  # talk to an in-process fake server (support/FakeServer.h).
  function(jfd_add_qt_test name)
    add_executable(${name} support/QtTestMain.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE doctest Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  jfd_add_qt_test(jfd_api_tests
    jellyfin-api/ValuesTest.cpp
    jellyfin-api/PlaybackTest.cpp
    jellyfin-api/ClientTest.cpp
    jellyfin-api/SessionSocketTest.cpp)
  target_link_libraries(jfd_api_tests PRIVATE jfd::jellyfin_api)

  jfd_add_qt_test(jfd_library_tests library/ItemBrowserTest.cpp)
  target_link_libraries(jfd_library_tests PRIVATE jfd::library)

  jfd_add_qt_test(jfd_platform_tests platform/CredentialStoreTest.cpp)
  target_link_libraries(jfd_platform_tests PRIVATE jfd::platform)

  if(JFD_SERVER_TESTS)
    # Needs JFD_TEST_SERVER_URL and friends; see scripts/test-server/README.md.
    jfd_add_qt_test(jfd_server_tests server/ServerTest.cpp)
    target_link_libraries(jfd_server_tests PRIVATE jfd::jellyfin_api jfd::library)
  endif()
endif()
