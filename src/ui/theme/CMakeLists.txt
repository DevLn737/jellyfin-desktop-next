# Jfd.Ui.Theme: colours, type, spacing and animation timing shared by every
# screen. QML only.
qt_add_library(jfd_ui_theme STATIC)
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(jfd_ui_theme
  URI Jfd.Ui.Theme
  VERSION 1.0
  QML_FILES Theme.qml)
target_link_libraries(jfd_ui_theme PRIVATE Qt6::Quick)
