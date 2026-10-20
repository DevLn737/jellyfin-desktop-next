# Jfd.Ui.Shell: the window's frame. Navigator (routes and the page stack),
# the sidebar, back and full-screen keys, the toast, and remembering the
# window's placement in settings.
qt_add_library(jfd_ui_shell STATIC
  src/Navigator.cpp
  src/WindowPlacement.cpp
  src/WindowStateKeeper.cpp
  include/jfd/ui/Navigator.h
  include/jfd/ui/WindowPlacement.h
  include/jfd/ui/WindowStateKeeper.h)
target_include_directories(jfd_ui_shell PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated QML type registration includes the headers by file name.
target_include_directories(jfd_ui_shell PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
qt_add_qml_module(jfd_ui_shell
  URI Jfd.Ui.Shell
  VERSION 1.0
  QML_FILES
    AppShell.qml
    PlaceholderPage.qml
    RoutePage.qml
    Sidebar.qml
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls)
target_link_libraries(jfd_ui_shell
  PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml jfd::settings
  PRIVATE Qt6::QuickControls2 jfd_ui_theme jfd_ui_controls)
target_compile_definitions(jfd_ui_shell PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_shell PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_shell)
