# Jfd.Ui.Controls: the shared building blocks of every screen (buttons,
# fields, cards, rows and grids of cards, lists, dialogs, toasts). Built on
# Qt Quick Templates, so keyboard focus and accessibility come from Qt and
# the look from Jfd.Ui.Theme. QML only.
qt_add_library(jfd_ui_controls STATIC)
set_source_files_properties(Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(jfd_ui_controls
  URI Jfd.Ui.Controls
  VERSION 1.0
  QML_FILES
    ActionButton.qml
    AppDialog.qml
    CardGrid.qml
    ChoiceBox.qml
    CardRow.qml
    FocusFrame.qml
    Heading.qml
    Icon.qml
    IconButton.qml
    Icons.qml
    InputField.qml
    ListItem.qml
    LoadingIndicator.qml
    PosterCard.qml
    SectionHeader.qml
    Toast.qml
    ToggleSwitch.qml
    ToolTipLabel.qml
    ValueSlider.qml
  RESOURCES
    icons/back.svg
    icons/check.svg
    icons/close.svg
    icons/error.svg
    icons/group.svg
    icons/home.svg
    icons/library.svg
    icons/menu.svg
    icons/play.svg
    icons/search.svg
    icons/settings.svg
    icons-dark/back.svg
    icons-dark/check.svg
    icons-dark/close.svg
    icons-dark/error.svg
    icons-dark/group.svg
    icons-dark/home.svg
    icons-dark/library.svg
    icons-dark/menu.svg
    icons-dark/play.svg
    icons-dark/search.svg
    icons-dark/settings.svg
  DEPENDENCIES Jfd.Ui.Theme)
target_link_libraries(jfd_ui_controls PRIVATE Qt6::Quick jfd_ui_theme)
jfd_add_translations(jfd_ui_controls)
