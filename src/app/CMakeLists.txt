qt_standard_project_setup()

qt_add_executable(jellyfin-desktop-next WIN32 MACOSX_BUNDLE
  main.cpp)

qt_add_qml_module(jellyfin-desktop-next
  URI JellyfinDesktop
  VERSION 1.0
  NO_PLUGIN
  QML_FILES qml/Main.qml)

target_link_libraries(jellyfin-desktop-next PRIVATE
  Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Quick Qt6::Qml mpv::mpv)

set_target_properties(jellyfin-desktop-next PROPERTIES OUTPUT_NAME jellyfin-desktop-next)

if(JFD_BUILD_TESTS)
  # Starts the window with libmpv, renders a few frames and exits with code 0.
  add_test(NAME app_smoke COMMAND jellyfin-desktop-next --smoke)
  set_tests_properties(app_smoke PROPERTIES TIMEOUT 60)
endif()
