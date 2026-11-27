# Jfd.Ui.Library: the sign-in, home, library, item and search screens, on
# the models of src/library. `Library` (LibraryUi) is the screens' way into
# those models; image://jfd/ serves card images through the library's image
# cache under a video memory budget. `Downloads` (DownloadsUi) is their way
# into the offline downloads (src/offline): the Download buttons and
# dialog, the badges, the Downloads page and the offline home rows.
qt_add_library(jfd_ui_library STATIC
  src/DownloadsUi.cpp
  src/LibraryImageProvider.cpp
  src/LibraryScreens.cpp
  src/LibraryUi.cpp
  include/jfd/ui/DownloadsUi.h
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
    AlphabetBar.qml
    ChannelPage.qml
    CollectionPage.qml
    CommandPalette.qml
    CustomizeHomePage.qml
    DiscordPrivacyPage.qml
    DownloadActions.qml
    DownloadDialog.qml
    DownloadRow.qml
    DownloadsPage.qml
    EpisodeCard.qml
    GenrePicker.qml
    GuidePage.qml
    HeroBackdrop.qml
    HeroBanner.qml
    HomePage.qml
    ItemCard.qml
    ItemPage.qml
    LibraryPage.qml
    LiveTvPage.qml
    LoginPage.qml
    PersonPage.qml
    RecordingsPage.qml
    RecordMenu.qml
    SearchPage.qml
  RESOURCES
    icons/livetv.svg
    icons/sites/anilist.svg
    icons/sites/imdb.svg
    icons/sites/myanimelist.svg
    icons/sites/shikimori.svg
    icons/sites/tmdb.svg
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell Jfd.Ui.Settings)
target_link_libraries(jfd_ui_library
  PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml jfd::account jfd::library jfd::offline jfd_ui_shell
  PRIVATE jfd_ui_theme jfd_ui_controls jfd_ui_settings)
target_compile_definitions(jfd_ui_library PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_library PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_library PLURALS)
