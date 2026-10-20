qt_add_executable(jellyfin-desktop-next WIN32 MACOSX_BUNDLE
  main.cpp
  AppController.cpp AppController.h
  FrameRecorder.cpp FrameRecorder.h
  Measurement.cpp Measurement.h)
target_compile_definitions(jellyfin-desktop-next PRIVATE JFD_VERSION="${PROJECT_VERSION}")

qt_add_qml_module(jellyfin-desktop-next
  URI JellyfinDesktop
  VERSION 1.0
  NO_PLUGIN
  QML_FILES qml/Main.qml qml/PlayerMenu.qml qml/OsdBar.qml qml/ProtocolOverlay.qml)

target_link_libraries(jellyfin-desktop-next PRIVATE
  Qt6::Core Qt6::Gui Qt6::OpenGL Qt6::Quick Qt6::Qml
  jfd::media_backend jfd::diagnostics)

set_target_properties(jellyfin-desktop-next PROPERTIES OUTPUT_NAME jellyfin-desktop-next)

if(JFD_BUILD_TESTS)
  # Plays a generated video, waits for the first frame and a few swaps, then
  # closes the window and exits with code 0.
  add_test(NAME app_smoke COMMAND jellyfin-desktop-next --smoke)
  set_tests_properties(app_smoke PROPERTIES TIMEOUT 60)
  if(WIN32)
    # The default on Windows is Direct3D 11; OpenGL stays available.
    add_test(NAME app_smoke_gl COMMAND jellyfin-desktop-next --smoke --renderer gl)
    set_tests_properties(app_smoke_gl PROPERTIES TIMEOUT 60)
  endif()
endif()
