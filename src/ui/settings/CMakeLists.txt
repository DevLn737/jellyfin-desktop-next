# Jfd.Ui.Settings: AppSettings (the settings store for QML) and the
# settings screen, generated from the registered sections.
qt_add_library(jfd_ui_settings STATIC
  src/SettingsModel.cpp
  include/jfd/ui/SettingsModel.h)
target_include_directories(jfd_ui_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated QML type registration includes the header by file name.
target_include_directories(jfd_ui_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
qt_add_qml_module(jfd_ui_settings
  URI Jfd.Ui.Settings
  VERSION 1.0
  QML_FILES
    SettingRow.qml
    SettingsPage.qml
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell)
target_link_libraries(jfd_ui_settings
  PUBLIC Qt6::Core Qt6::Qml jfd::settings
  PRIVATE Qt6::Quick jfd_ui_theme jfd_ui_controls jfd_ui_shell)
target_compile_definitions(jfd_ui_settings PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_settings PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_settings)
