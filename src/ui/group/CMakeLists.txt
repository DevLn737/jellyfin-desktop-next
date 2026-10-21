# Jfd.Ui.Group: watching together. The group page (the server's groups,
# create, join, members, speed, queue, leave) and the group indicator for
# the player's OSD, both bound to the session's GroupModel (src/session),
# which the application hands to QML as the GroupSession singleton.
qt_add_library(jfd_ui_group STATIC
  include/jfd/ui/GroupSession.h)
target_include_directories(jfd_ui_group PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated QML type registration includes the header by file name.
target_include_directories(jfd_ui_group PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
qt_add_qml_module(jfd_ui_group
  URI Jfd.Ui.Group
  VERSION 1.0
  QML_FILES
    GroupOsd.qml
    GroupPage.qml
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell)
target_link_libraries(jfd_ui_group
  PUBLIC Qt6::Core Qt6::Qml jfd::session_runtime
  PRIVATE Qt6::Quick jfd_ui_theme jfd_ui_controls jfd_ui_shell)
target_compile_definitions(jfd_ui_group PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_group PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_group)
