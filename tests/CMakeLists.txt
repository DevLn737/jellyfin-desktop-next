# doctest 2.4.11 single header, vendored in third_party/doctest (MIT).
add_library(doctest INTERFACE)
target_include_directories(doctest SYSTEM INTERFACE ${PROJECT_SOURCE_DIR}/third_party/doctest)

add_library(jfd_test_support INTERFACE)
target_include_directories(jfd_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(jfd_test_support INTERFACE jfd::core)

add_executable(jfd_core_tests
  core/main.cpp
  core/TimeTest.cpp
  core/EpochTest.cpp
  core/InterfacesTest.cpp
  core/TimelineTest.cpp
  core/ReadinessTest.cpp
  core/ReconcilerTest.cpp
  core/PlaybackSessionTest.cpp)
target_link_libraries(jfd_core_tests PRIVATE jfd::core jfd_test_support doctest)
add_test(NAME jfd_core_tests COMMAND jfd_core_tests)

add_executable(jfd_diagnostics_tests
  core/main.cpp
  diagnostics/FrameStatsTest.cpp
  diagnostics/ProcessStatsTest.cpp)
target_link_libraries(jfd_diagnostics_tests PRIVATE jfd::diagnostics doctest)
add_test(NAME jfd_diagnostics_tests COMMAND jfd_diagnostics_tests)


add_executable(jfd_syncplay_tests
  core/main.cpp
  syncplay/ProtocolTest.cpp
  syncplay/TimeSyncTest.cpp
  syncplay/GroupClientTest.cpp)
target_link_libraries(jfd_syncplay_tests PRIVATE jfd::syncplay jfd_test_support doctest nlohmann_json)
target_compile_definitions(jfd_syncplay_tests PRIVATE
  JFD_SYNCPLAY_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/syncplay/fixtures")
add_test(NAME jfd_syncplay_tests COMMAND jfd_syncplay_tests)

add_executable(jfd_session_tests core/main.cpp session/SessionHubTest.cpp)
target_link_libraries(jfd_session_tests PRIVATE jfd::session doctest)
add_test(NAME jfd_session_tests COMMAND jfd_session_tests)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  # GCC reports false positives for std::variant copies of the recording fakes at -O2+.
  target_compile_options(jfd_core_tests PRIVATE -Wno-maybe-uninitialized)
  target_compile_options(jfd_syncplay_tests PRIVATE -Wno-maybe-uninitialized)
endif()

# Deterministic simulator (see tests/sim/Simulation.h).
add_executable(jfd_simulator sim/main.cpp sim/Simulation.cpp)
target_link_libraries(jfd_simulator PRIVATE jfd::core)
add_test(NAME simulator_chaos COMMAND jfd_simulator --mode chaos --scenarios ${JFD_SIM_SCENARIOS} --seed 1)
add_test(NAME simulator_drift_1x COMMAND jfd_simulator --mode drift --scenarios 3 --duration 1830 --rate 1)
add_test(NAME simulator_drift_1_25x COMMAND jfd_simulator --mode drift --scenarios 3 --duration 1830 --rate 1.25)
add_test(NAME simulator_drift_1_5x COMMAND jfd_simulator --mode drift --scenarios 3 --duration 1830 --rate 1.5)
add_test(NAME simulator_drift_external_audio COMMAND jfd_simulator --mode drift --scenarios 3 --duration 600 --rate 1.25 --external-audio)
add_test(NAME simulator_stock_server COMMAND jfd_simulator --mode chaos --scenarios 200 --seed 500000 --stock-server)

# Mutation check: each mutant is core with one injected bug. The simulator
# must report violations, so these tests pass only when it does.
foreach(mutant IN ITEMS "stale_epoch;1" "stale_ready;2" "late_timer;3")
  list(GET mutant 0 name)
  list(GET mutant 1 number)
  jfd_core_mutant(${name} ${number})
  add_executable(jfd_simulator_mutant_${name} sim/main.cpp sim/Simulation.cpp)
  target_link_libraries(jfd_simulator_mutant_${name} PRIVATE jfd_core_mutant_${name})
  add_test(NAME mutation_${name} COMMAND jfd_simulator_mutant_${name} --mode chaos --scenarios 300 --seed 1)
  set_tests_properties(mutation_${name} PROPERTIES PASS_REGULAR_EXPRESSION "violations=[1-9]")
endforeach()

if(TARGET jfd_jellyfin_api)
  find_package(Qt6 REQUIRED COMPONENTS Test)

  # Qt modules are tested with doctest under a QCoreApplication; network tests
  # talk to an in-process fake server (support/FakeServer.h).
  function(jfd_add_qt_test name)
    add_executable(${name} support/QtTestMain.cpp ${ARGN})
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PRIVATE doctest Qt6::Test)
    # See src/jellyfin-api/CMakeLists.txt: C4702 from inlined Qt headers.
    target_compile_options(${name} PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME ${name} COMMAND ${name})
  endfunction()

  jfd_add_qt_test(jfd_api_tests
    jellyfin-api/ValuesTest.cpp
    jellyfin-api/PlaybackTest.cpp
    jellyfin-api/ClientTest.cpp
    jellyfin-api/LibraryApiTest.cpp
    jellyfin-api/SessionSocketTest.cpp)
  target_link_libraries(jfd_api_tests PRIVATE jfd::jellyfin_api)

  jfd_add_qt_test(jfd_library_tests
    library/ItemBrowserTest.cpp
    library/HomeModelTest.cpp
    library/LibraryModelTest.cpp
    library/DetailsSearchTest.cpp
    library/ImageCacheTest.cpp
    library/SignInTest.cpp)
  target_link_libraries(jfd_library_tests PRIVATE jfd::library)

  jfd_add_qt_test(jfd_playback_tests playback/TrackTableTest.cpp playback/PlaybackReporterTest.cpp
    playback/StreamPlanTest.cpp playback/TrackControllerTest.cpp
    playback/SegmentSkipperTest.cpp)
  target_link_libraries(jfd_playback_tests PRIVATE jfd::playback)

  if(TARGET jfd_integrations)
    jfd_add_qt_test(jfd_integrations_tests integrations/DiscordTest.cpp integrations/UpdateCheckerTest.cpp)
    target_link_libraries(jfd_integrations_tests PRIVATE jfd::integrations Qt6::WebSockets)
  endif()

  jfd_add_qt_test(jfd_platform_tests platform/CredentialStoreTest.cpp platform/SingleInstanceTest.cpp)
  target_link_libraries(jfd_platform_tests PRIVATE jfd::platform)
  find_package(Qt6 QUIET COMPONENTS DBus)
  if(Qt6DBus_FOUND AND NOT WIN32 AND NOT APPLE)
    # Against fake portal, screen saver and MPRIS clients on a private dbus-daemon.
    jfd_add_qt_test(jfd_platform_dbus_tests platform/SleepInhibitorTest.cpp platform/MprisTest.cpp)
    target_link_libraries(jfd_platform_dbus_tests PRIVATE jfd::platform)
    set_target_properties(jfd_platform_dbus_tests PROPERTIES AUTOMOC ON)
  endif()

  if(JFD_SERVER_TESTS)
    # Needs JFD_TEST_SERVER_URL and friends; see scripts/test-server/README.md.
    jfd_add_qt_test(jfd_server_tests server/ServerTest.cpp)
    target_link_libraries(jfd_server_tests PRIVATE jfd::jellyfin_api jfd::library)

    # The session runtime in a live group, on a virtual backend.
    jfd_add_qt_test(jfd_session_server_tests session/SessionRuntimeServerTest.cpp)
    target_link_libraries(jfd_session_server_tests PRIVATE jfd::session_runtime)
    # One live server: these tests do not run at the same time as each other.
    set_tests_properties(jfd_server_tests jfd_session_server_tests PROPERTIES RESOURCE_LOCK jellyfin_server)
    set_tests_properties(jfd_session_server_tests PROPERTIES TIMEOUT 300)
  endif()
endif()

if(JFD_BUILD_APP)
  add_subdirectory(render)

  # core::MediaBackend on a headless mpv (no video output, null audio).
  add_executable(jfd_media_backend_tests support/QtTestMain.cpp media/MpvMediaBackendTest.cpp)
  target_link_libraries(jfd_media_backend_tests PRIVATE jfd::media_backend doctest)
  add_test(NAME jfd_media_backend_tests COMMAND jfd_media_backend_tests)
  set_tests_properties(jfd_media_backend_tests PROPERTIES TIMEOUT 120)

  if(JFD_SERVER_TESTS AND TARGET jfd_session_runtime)
    # The same on headless mpv, playing the server's stream.
    jfd_add_qt_test(jfd_session_mpv_server_tests session/SessionRuntimeServerTest.cpp)
    target_link_libraries(jfd_session_mpv_server_tests PRIVATE jfd::session_runtime jfd::media_backend)
    target_compile_definitions(jfd_session_mpv_server_tests PRIVATE JFD_SESSION_TEST_MPV)
    set_tests_properties(jfd_session_mpv_server_tests PROPERTIES TIMEOUT 300 RESOURCE_LOCK jellyfin_server)
  endif()
endif()
