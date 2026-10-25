# Jfd.Ui.Library: the sign-in, home, library, item and search screens, on
# the models of src/library. `Library` (LibraryUi) is the screens' way into
# those models; image://jfd/ serves card images through the library's image
# cache under a video memory budget.
qt_add_library(jfd_ui_library STATIC
  src/LibraryImageProvider.cpp
  src/LibraryScreens.cpp
  src/LibraryUi.cpp
  include/jfd/ui/LibraryImageProvider.h
  include/jfd/ui/LibraryScreens.h
  include/jfd/ui/LibraryTypes.h
  include/jfd/ui/LibraryUi.h)
target_include_directories(jfd_ui_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated QML type registration includes the headers by file name.
target_include_directories(jfd_ui_library PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
qt_add_qml_module(jfd_ui_library
  URI Jfd.Ui.Library
  VERSION 1.0
  QML_FILES
    CollectionPage.qml
    EpisodeRow.qml
    GenrePicker.qml
    HomePage.qml
    ItemCard.qml
    ItemPage.qml
    LibraryPage.qml
    LoginPage.qml
    SearchPage.qml
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell)
target_link_libraries(jfd_ui_library
  PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml jfd::library jfd_ui_shell
  PRIVATE jfd_ui_theme jfd_ui_controls)
target_compile_definitions(jfd_ui_library PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_library PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_library)
