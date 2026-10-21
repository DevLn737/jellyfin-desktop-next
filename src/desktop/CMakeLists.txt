# The application: jellyfin-desktop. Wires the modules together (AppServices),
# opens the main window and handles startup and shutdown order.
qt_add_executable(jellyfin-desktop WIN32 MACOSX_BUNDLE
  main.cpp
  AppServices.cpp AppServices.h
  FlowCheck.cpp FlowCheck.h
  IntegrationServices.cpp IntegrationServices.h
  GroupInstall.cpp
  LibraryInstall.cpp
  SessionInstall.cpp
  PlayerInstall.cpp
  Logging.cpp Logging.h
  SmokeTest.cpp SmokeTest.h
  Translations.cpp Translations.h)
target_compile_definitions(jellyfin-desktop PRIVATE JFD_VERSION="${PROJECT_VERSION}")
set_target_properties(jellyfin-desktop PROPERTIES AUTOMOC ON)

qt_add_qml_module(jellyfin-desktop
  URI Jfd.Desktop
  VERSION 1.0
  NO_PLUGIN
  QML_FILES Main.qml
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Shell Jfd.Ui.Settings Jfd.Ui.Library Jfd.Ui.Group Jfd.Ui.Player)

# Static QML modules: link each one's plugin (Q_IMPORT_QML_PLUGIN in
# main.cpp) as well as the module itself.
target_link_libraries(jellyfin-desktop PRIVATE
  Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml Qt6::QuickControls2 Qt6::Network
  jfd::settings jfd::integrations jfd::platform jfd::session jfd::jellyfin_api
  jfd::library jfd::session_runtime jfd::media_backend
  jfd_ui_theme jfd_ui_themeplugin
  jfd_ui_controls jfd_ui_controlsplugin
  jfd_ui_shell jfd_ui_shellplugin
  jfd_ui_settings jfd_ui_settingsplugin
  jfd_ui_library jfd_ui_libraryplugin
  jfd_ui_group jfd_ui_groupplugin
  jfd_ui_player jfd_ui_playerplugin)
jfd_add_translations(jellyfin-desktop)

if(JFD_BUILD_TESTS)
  # Opens the window, checks the login page, visits every route and back,
  # quits, and fails if shutdown takes a second or more.
  add_test(NAME desktop_smoke COMMAND jellyfin-desktop --smoke)
  set_tests_properties(desktop_smoke PROPERTIES TIMEOUT 60)
  if(JFD_SERVER_TESTS)
    # Against scripts/test-server: sign in, series, episode, play, switch
    # audio and subtitles, quit (checkpoint 1).
    add_test(NAME desktop_flow COMMAND jellyfin-desktop --flow)
    set_tests_properties(desktop_flow PROPERTIES TIMEOUT 240)
  endif()
endif()
