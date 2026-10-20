# Renders mpv video inside a Qt Quick window with a QML overlay and checks the
# pixels: orientation, placement in a sub-rectangle, blending over video, Qt
# state after mpv has drawn, and ordered teardown. Runs on software OpenGL
# (Mesa under Xvfb on Linux, Qt's opengl32sw on Windows).
qt_add_executable(jfd_render_test RenderTest.cpp)
qt_add_qml_module(jfd_render_test
  URI JellyfinDesktop.RenderTest
  VERSION 1.0
  NO_PLUGIN
  QML_FILES RenderScene.qml)
target_link_libraries(jfd_render_test PRIVATE Qt6::Gui Qt6::Quick Qt6::Qml jfd::media_backend)
add_test(NAME jfd_render_test COMMAND jfd_render_test)
set_tests_properties(jfd_render_test PROPERTIES TIMEOUT 90)
