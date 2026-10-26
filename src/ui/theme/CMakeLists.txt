# Jfd.Ui.Theme: colours, type, spacing and animation timing shared by every
# screen, and the colour palettes behind them as data (built-in palettes,
# accent colours, WCAG contrast, themes read from files).
qt_add_library(jfd_ui_theme STATIC
  src/ThemeFile.cpp
  src/ThemePalette.cpp
  include/jfd/ui/ThemeFile.h
  include/jfd/ui/ThemePalette.h)
target_include_directories(jfd_ui_theme PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_source_files_properties(Theme.qml Palettes.qml Overlay.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(jfd_ui_theme
  URI Jfd.Ui.Theme
  VERSION 1.0
  QML_FILES Theme.qml Palettes.qml Overlay.qml)
target_link_libraries(jfd_ui_theme PUBLIC Qt6::Core Qt6::Gui PRIVATE Qt6::Quick)
target_compile_definitions(jfd_ui_theme PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
jfd_add_translations(jfd_ui_theme)
