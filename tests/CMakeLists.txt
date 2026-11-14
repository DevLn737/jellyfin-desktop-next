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
  core/PlaybackSessionTest.cpp
  core/SessionJournalTest.cpp)
target_link_libraries(jfd_core_tests PRIVATE jfd::core jfd_test_support doctest)
add_test(NAME jfd_core_tests COMMAND jfd_core_tests)

# QML strings people read go through qsTr(), so translations_check sees them.
add_test(NAME qml_literals_check
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/i18n/QmlLiterals.cmake")

# Every file in an icons*/ folder has its line in LICENSES.md, and every
# line there names a file that exists.
add_test(NAME licenses_check
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/licenses/IconLicenses.cmake")

# The MSI versions packaging/release-version.sh derives from release tags.
find_program(JFD_BASH bash)
if(UNIX AND JFD_BASH)
  add_test(NAME release_version_check
    COMMAND "${JFD_BASH}" "${CMAKE_CURRENT_SOURCE_DIR}/packaging/release-version-test.sh" "${PROJECT_SOURCE_DIR}")
endif()

add_executable(jfd_diagnostics_tests
  core/main.cpp
  diagnostics/FrameStatsTest.cpp
  diagnostics/ProcessStatsTest.cpp
  diagnostics/RedactTest.cpp
  diagnostics/ReportTest.cpp)
target_link_libraries(jfd_diagnostics_tests PRIVATE jfd::diagnostics doctest)
add_test(NAME jfd_diagnostics_tests COMMAND jfd_diagnostics_tests)


add_executable(jfd_syncplay_tests
  core/main.cpp
  syncplay/ProtocolTest.cpp
  syncplay/TimeSyncTest.cpp
  syncplay/GroupClientTest.cpp
  syncplay/RecordedTest.cpp)
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
    jellyfin-api/MusicApiTest.cpp
    jellyfin-api/LiveTvApiTest.cpp
    jellyfin-api/SessionSocketTest.cpp)
  target_link_libraries(jfd_api_tests PRIVATE jfd::jellyfin_api)

  jfd_add_qt_test(jfd_library_tests
    library/ItemBrowserTest.cpp
    library/HomeModelTest.cpp
    library/HomeSectionTest.cpp
    library/RecommendationRowsTest.cpp
    library/LibraryModelTest.cpp
    library/DetailsSearchTest.cpp
    library/ImageCacheTest.cpp
    library/LiveTvChannelsTest.cpp
    library/SignInTest.cpp)
  target_link_libraries(jfd_library_tests PRIVATE jfd::library)

  jfd_add_qt_test(jfd_playback_tests playback/TrackTableTest.cpp playback/PlaybackReporterTest.cpp
    playback/StreamPlanTest.cpp playback/TrackControllerTest.cpp
    playback/SegmentSkipperTest.cpp playback/TrackPreferenceTest.cpp playback/PlayerOptionsTest.cpp)
  target_link_libraries(jfd_playback_tests PRIVATE jfd::playback jfd_test_support)

  if(TARGET jfd_integrations)
    jfd_add_qt_test(jfd_integrations_tests integrations/DiscordTest.cpp integrations/UpdateCheckerTest.cpp
      integrations/MediaSessionBridgeTest.cpp integrations/IntegrationsSettingsTest.cpp
      integrations/ReleaseSignatureTest.cpp integrations/AppImageUpdaterTest.cpp
      integrations/MsiUpdaterTest.cpp integrations/SignedDownloadTest.cpp)
    target_link_libraries(jfd_integrations_tests PRIVATE jfd::integrations Qt6::WebSockets)
  endif()

  if(TARGET jfd_remote)
    # Remote control on fake server messages, and --command from a second start.
    jfd_add_qt_test(jfd_remote_tests remote/RemoteCommandsTest.cpp remote/RemoteControlTest.cpp)
    target_link_libraries(jfd_remote_tests PRIVATE jfd::remote jfd::platform Qt6::WebSockets)
    if(JFD_SERVER_TESTS)
      # A second session of the test user sends Play, Playstate and
      # GeneralCommand through the live server.
      jfd_add_qt_test(jfd_remote_server_tests remote/RemoteServerTest.cpp)
      target_link_libraries(jfd_remote_server_tests PRIVATE jfd::remote Qt6::WebSockets)
      set_tests_properties(jfd_remote_server_tests PROPERTIES TIMEOUT 120 RESOURCE_LOCK jellyfin_server)
    endif()
  endif()

  if(TARGET jfd_offline)
    # Offline downloads against a file server with byte ranges
    # (offline/FakeFileServer.h); the kill test runs jfd_offline_killee.
    jfd_add_qt_test(jfd_offline_tests offline/StoreTest.cpp offline/DownloadTest.cpp
      offline/CatalogTest.cpp offline/KillTest.cpp)
    target_link_libraries(jfd_offline_tests PRIVATE jfd::offline Qt6::WebSockets)
    add_executable(jfd_offline_killee offline/Killee.cpp)
    target_link_libraries(jfd_offline_killee PRIVATE jfd::offline)
    target_compile_options(jfd_offline_killee PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_dependencies(jfd_offline_tests jfd_offline_killee)
    target_compile_definitions(jfd_offline_tests PRIVATE
      JFD_OFFLINE_KILLEE="$<TARGET_FILE:jfd_offline_killee>")
    set_tests_properties(jfd_offline_tests PROPERTIES TIMEOUT 300)
  endif()

  if(TARGET jfd_music)
    jfd_add_qt_test(jfd_music_tests music/MusicQueueTest.cpp)
    target_link_libraries(jfd_music_tests PRIVATE jfd::music)
    if(JFD_SERVER_TESTS)
      # The music player on headless mpv against the extras server.
      jfd_add_qt_test(jfd_music_server_tests music/MusicServerTest.cpp)
      target_link_libraries(jfd_music_server_tests PRIVATE jfd::music jfd::media_backend)
      set_tests_properties(jfd_music_server_tests PROPERTIES TIMEOUT 300 RESOURCE_LOCK jellyfin_server)
    endif()
  endif()

  if(TARGET jfd_seerr)
    jfd_add_qt_test(jfd_seerr_tests seerr/SeerrClientTest.cpp seerr/SeerrServiceTest.cpp)
    target_link_libraries(jfd_seerr_tests PRIVATE jfd::seerr)
  # FakeSeerr.h writes its JSON fixtures as plain literals.
  if(NOT MSVC)
    target_compile_options(jfd_seerr_tests PRIVATE -UQT_NO_CAST_FROM_ASCII)
  else()
    target_compile_options(jfd_seerr_tests PRIVATE /UQT_NO_CAST_FROM_ASCII)
  endif()
  endif()

  jfd_add_qt_test(jfd_platform_tests platform/CredentialStoreTest.cpp platform/SingleInstanceTest.cpp
    platform/GamepadMapperTest.cpp)
  target_link_libraries(jfd_platform_tests PRIVATE jfd::platform)
  if(JFD_GAMEPAD)
    # GamepadInput on SDL's virtual joystick: layouts (Xbox, 8BitDo in
    # XInput, Switch and DirectInput modes), repeat, dead zone, unplugging.
    jfd_add_qt_test(jfd_platform_gamepad_tests platform/GamepadInputTest.cpp)
    target_link_libraries(jfd_platform_gamepad_tests PRIVATE jfd::platform jfd_sdl3)
  endif()
  find_package(Qt6 QUIET COMPONENTS DBus)
  if(Qt6DBus_FOUND AND NOT WIN32 AND NOT APPLE)
    # Against fake portal, screen saver and MPRIS clients on a private dbus-daemon.
    jfd_add_qt_test(jfd_platform_dbus_tests platform/SleepInhibitorTest.cpp platform/MprisTest.cpp)
    target_link_libraries(jfd_platform_dbus_tests PRIVATE jfd::platform)
    set_target_properties(jfd_platform_dbus_tests PROPERTIES AUTOMOC ON)
  endif()

  if(TARGET jfd_session_runtime)
    jfd_add_qt_test(jfd_session_runtime_tests session/SleepDetectorTest.cpp session/GroupModelTest.cpp)
    target_link_libraries(jfd_session_runtime_tests PRIVATE jfd::session_runtime)
  endif()

  if(TARGET jfd_settings)
    jfd_add_qt_test(jfd_settings_tests settings/SettingsTest.cpp settings/LegacyImportTest.cpp)
    target_link_libraries(jfd_settings_tests PRIVATE jfd::settings)
    target_compile_definitions(jfd_settings_tests PRIVATE
      JFD_LEGACY_PROFILE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/settings/legacy-2x")
  endif()

  if(JFD_SERVER_TESTS)
    # Needs JFD_TEST_SERVER_URL and friends; see scripts/test-server/README.md.
    jfd_add_qt_test(jfd_server_tests server/ServerTest.cpp)
    target_link_libraries(jfd_server_tests PRIVATE jfd::jellyfin_api jfd::library)

    # The session runtime in a live group, on a virtual backend.
    jfd_add_qt_test(jfd_session_server_tests session/SessionRuntimeServerTest.cpp)
    target_link_libraries(jfd_session_server_tests PRIVATE jfd::session_runtime)
    # Live TV on the extras stand (M3U tuner, XMLTV guide); skipped without it.
    jfd_add_qt_test(jfd_livetv_server_tests server/LiveTvServerTest.cpp)
    target_link_libraries(jfd_livetv_server_tests PRIVATE jfd::jellyfin_api jfd::library)

    # One live server: these tests do not run at the same time as each other.
    set_tests_properties(jfd_server_tests jfd_session_server_tests jfd_livetv_server_tests
      PROPERTIES RESOURCE_LOCK jellyfin_server)
    set_tests_properties(jfd_livetv_server_tests PROPERTIES TIMEOUT 120)
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

  # The video items' render-thread decisions, without a GPU.
  add_executable(jfd_render_setup_tests support/QtGuiTestMain.cpp media/RenderSetupTest.cpp)
  target_link_libraries(jfd_render_setup_tests PRIVATE jfd::media_backend doctest)
  add_test(NAME jfd_render_setup_tests COMMAND jfd_render_setup_tests)
  set_tests_properties(jfd_render_setup_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

  if(JFD_SERVER_TESTS AND TARGET jfd_session_runtime)
    # The same on headless mpv, playing the server's stream.
    jfd_add_qt_test(jfd_session_mpv_server_tests session/SessionRuntimeServerTest.cpp)
    target_link_libraries(jfd_session_mpv_server_tests PRIVATE jfd::session_runtime jfd::media_backend)
    target_compile_definitions(jfd_session_mpv_server_tests PRIVATE JFD_SESSION_TEST_MPV)
    set_tests_properties(jfd_session_mpv_server_tests PROPERTIES TIMEOUT 300 RESOURCE_LOCK jellyfin_server)
  endif()

  if(TARGET jfd_ui_theme)
    # Palettes, accent colours, contrast and theme files.
    add_executable(jfd_ui_theme_tests support/QtTestMain.cpp ui/ThemeTest.cpp)
    target_link_libraries(jfd_ui_theme_tests PRIVATE jfd_ui_theme doctest Qt6::Test)
    add_test(NAME jfd_ui_theme_tests COMMAND jfd_ui_theme_tests)
  endif()

  if(TARGET jfd_ui_shell)
    add_executable(jfd_ui_tests support/QtTestMain.cpp ui/ShellTest.cpp)
    target_link_libraries(jfd_ui_tests PRIVATE jfd_ui_shell doctest Qt6::Test)
    add_test(NAME jfd_ui_tests COMMAND jfd_ui_tests)
    # The shell's connection points in a window: the mini player strip,
    # card shapes, the palette.
    add_executable(jfd_ui_shell_page_tests support/QtGuiTestMain.cpp ui/ShellPageTest.cpp)
    target_link_libraries(jfd_ui_shell_page_tests PRIVATE
      jfd_ui_theme jfd_ui_themeplugin jfd_ui_controls jfd_ui_controlsplugin
      jfd_ui_shell jfd_ui_shellplugin Qt6::Quick doctest Qt6::Test)
    target_compile_options(jfd_ui_shell_page_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_shell_page_tests COMMAND jfd_ui_shell_page_tests)
    set_tests_properties(jfd_ui_shell_page_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(TARGET jfd_ui_library AND TARGET jfd_ui_shell)
    # Ctrl+K: the command palette over the shell.
    add_executable(jfd_ui_palette_tests support/QtGuiTestMain.cpp ui/PaletteTest.cpp)
    target_link_libraries(jfd_ui_palette_tests PRIVATE
      jfd_ui_theme jfd_ui_themeplugin jfd_ui_controls jfd_ui_controlsplugin
      jfd_ui_shell jfd_ui_shellplugin jfd_ui_library jfd_ui_libraryplugin Qt6::Quick doctest Qt6::Test)
    target_compile_options(jfd_ui_palette_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_palette_tests COMMAND jfd_ui_palette_tests)
    set_tests_properties(jfd_ui_palette_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(JFD_GAMEPAD AND TARGET jfd_ui_library AND TARGET jfd_ui_shell)
    # A gamepad (SDL's virtual joystick) driving a window through
    # InputRouter: button to focus change < 30 ms, Back, Select, Search,
    # media actions to the session, repeat, unplugging; and on the real
    # shell, Back leaves the page and closes the palette.
    add_executable(jfd_ui_gamepad_tests support/QtGuiTestMain.cpp ui/GamepadFocusTest.cpp ui/GamepadShellTest.cpp)
    target_link_libraries(jfd_ui_gamepad_tests PRIVATE
      jfd_ui_theme jfd_ui_themeplugin jfd_ui_controls jfd_ui_controlsplugin
      jfd_ui_shell jfd_ui_shellplugin jfd_ui_library jfd_ui_libraryplugin
      jfd::platform jfd_sdl3 Qt6::Quick doctest Qt6::Test)
    target_compile_options(jfd_ui_gamepad_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_gamepad_tests COMMAND jfd_ui_gamepad_tests)
    set_tests_properties(jfd_ui_gamepad_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(TARGET jfd_ui_library)
    add_executable(jfd_ui_library_tests support/QtTestMain.cpp ui/LibraryUiTest.cpp)
    target_link_libraries(jfd_ui_library_tests PRIVATE jfd_ui_library doctest Qt6::Test)
    target_compile_options(jfd_ui_library_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_library_tests COMMAND jfd_ui_library_tests)
  endif()

  if(TARGET jfd_ui_requests AND TARGET jfd_ui_shell AND TARGET jfd_ui_library)
    # The requests screens on a fake Seerr, in an offscreen window.
    add_executable(jfd_ui_requests_tests support/QtGuiTestMain.cpp ui/RequestsPageTest.cpp)
    target_include_directories(jfd_ui_requests_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_ui_requests_tests PRIVATE
      jfd_ui_requests jfd_ui_requestsplugin
      jfd_ui_theme jfd_ui_themeplugin jfd_ui_controls jfd_ui_controlsplugin
      jfd_ui_shell jfd_ui_shellplugin jfd_ui_library jfd_ui_libraryplugin
      jfd::seerr Qt6::Quick doctest Qt6::Test)
    target_compile_options(jfd_ui_requests_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    # FakeSeerr.h writes its JSON fixtures as plain literals.
    if(NOT MSVC)
      target_compile_options(jfd_ui_requests_tests PRIVATE -UQT_NO_CAST_FROM_ASCII)
    else()
      target_compile_options(jfd_ui_requests_tests PRIVATE /UQT_NO_CAST_FROM_ASCII)
    endif()
    add_test(NAME jfd_ui_requests_tests COMMAND jfd_ui_requests_tests)
    set_tests_properties(jfd_ui_requests_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(TARGET jfd_ui_player)
    # The player page on the session runtime with a virtual backend.
    add_executable(jfd_ui_player_tests support/QtGuiTestMain.cpp ui/PlayerPageTest.cpp)
    target_include_directories(jfd_ui_player_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_ui_player_tests PRIVATE
      jfd_ui_player jfd_ui_playerplugin jfd_ui_group jfd_ui_groupplugin
      jfd_ui_themeplugin jfd_ui_controlsplugin jfd_ui_shellplugin
      jfd::session_runtime Qt6::Gui Qt6::Quick doctest Qt6::Test)
    target_compile_options(jfd_ui_player_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_player_tests COMMAND jfd_ui_player_tests)
    set_tests_properties(jfd_ui_player_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(TARGET jfd_ui_music)
    # The music screens on a virtual backend and a fake server: 10,000
    # albums and 50,000 tracks by pages, the mini-player, groups and the
    # question before music replaces a movie.
    add_executable(jfd_ui_music_tests support/QtGuiTestMain.cpp ui/MusicUiTest.cpp ui/MusicLyricsTest.cpp)
    target_include_directories(jfd_ui_music_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_ui_music_tests PRIVATE
      jfd_ui_music jfd_ui_musicplugin jfd_ui_library jfd_ui_libraryplugin
      jfd_ui_themeplugin jfd_ui_controlsplugin jfd_ui_shellplugin
      jfd::session_runtime Qt6::Gui Qt6::Quick Qt6::WebSockets doctest Qt6::Test)
    target_compile_options(jfd_ui_music_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_music_tests COMMAND jfd_ui_music_tests)
    set_tests_properties(jfd_ui_music_tests PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(JFD_SERVER_TESTS AND TARGET jfd_ui_player)
    # The player screen's model on headless mpv against the server: tracks,
    # a transcoded seek and the server's playback reports.
    add_executable(jfd_ui_player_server_tests support/QtGuiTestMain.cpp ui/PlayerServerTest.cpp)
    target_include_directories(jfd_ui_player_server_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_ui_player_server_tests PRIVATE
      jfd_ui_player jfd::session_runtime jfd::media_backend Qt6::Gui Qt6::Quick doctest Qt6::Test)
    target_compile_options(jfd_ui_player_server_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_player_server_tests COMMAND jfd_ui_player_server_tests)
    set_tests_properties(jfd_ui_player_server_tests PROPERTIES
      TIMEOUT 300 RESOURCE_LOCK jellyfin_server ENVIRONMENT QT_QPA_PLATFORM=offscreen)
  endif()

  if(JFD_SERVER_TESTS AND TARGET jfd_ui_library)
    # The library screens in a window, driven against the server.
    add_executable(jfd_ui_library_server_tests support/QtGuiTestMain.cpp ui/LibraryScreensServerTest.cpp)
    target_include_directories(jfd_ui_library_server_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_ui_library_server_tests PRIVATE
      jfd_ui_theme jfd_ui_themeplugin jfd_ui_controls jfd_ui_controlsplugin
      jfd_ui_shell jfd_ui_shellplugin jfd_ui_library jfd_ui_libraryplugin
      doctest Qt6::Test Qt6::Quick)
    target_compile_options(jfd_ui_library_server_tests PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
    add_test(NAME jfd_ui_library_server_tests COMMAND jfd_ui_library_server_tests)
    set_tests_properties(jfd_ui_library_server_tests PROPERTIES TIMEOUT 300 RESOURCE_LOCK jellyfin_server)
  endif()

  if(TARGET jfd_ui_library)
    # Timings for the startup, home and library-scroll thresholds; run by
    # scripts/library-bench.sh, not by ctest.
    add_executable(jfd_library_bench ui/LibraryBench.cpp)
    target_include_directories(jfd_library_bench PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_library_bench PRIVATE
      jfd_ui_theme jfd_ui_themeplugin jfd_ui_controls jfd_ui_controlsplugin
      jfd_ui_shell jfd_ui_shellplugin jfd_ui_library jfd_ui_libraryplugin
      Qt6::Quick Qt6::WebSockets)
    target_compile_options(jfd_library_bench PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
  endif()

  if(JFD_SERVER_TESTS AND TARGET jfd_ui_group)
    # The group page on a live server, on a virtual backend.
    add_executable(jfd_ui_group_server_tests ui/GroupPageTest.cpp)
    target_include_directories(jfd_ui_group_server_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(jfd_ui_group_server_tests PRIVATE
      jfd_ui_group jfd_ui_groupplugin jfd_ui_themeplugin jfd_ui_controlsplugin jfd_ui_shellplugin
      jfd::session_runtime Qt6::Gui Qt6::Quick doctest Qt6::Test)
    add_test(NAME jfd_ui_group_server_tests COMMAND jfd_ui_group_server_tests)
    set_tests_properties(jfd_ui_group_server_tests PROPERTIES TIMEOUT 300 RESOURCE_LOCK jellyfin_server)
  endif()
endif()
