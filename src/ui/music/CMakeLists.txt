# Jfd.Ui.Music: the music screens (library, album, artist, playlist, genre,
# Now Playing) and the mini-player, on the lists of src/ui/music and the
# player of src/music. `Music` is the screens' way into the lists,
# `MusicSession` the player.
qt_add_library(jfd_ui_music STATIC
  src/MusicItem.cpp
  src/MusicListModel.cpp
  src/MusicUi.cpp
  include/jfd/ui/MusicItem.h
  include/jfd/ui/MusicListModel.h
  include/jfd/ui/MusicUi.h)
target_include_directories(jfd_ui_music PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(jfd_ui_music PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
set_source_files_properties(MusicIcons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(jfd_ui_music
  URI Jfd.Ui.Music
  VERSION 1.0
  QML_FILES
    ActionMenu.qml
    AlbumPage.qml
    ArtistPage.qml
    GenrePage.qml
    GroupHint.qml
    MiniPlayer.qml
    MusicCard.qml
    MusicIcons.qml
    MusicPage.qml
    NowPlayingPage.qml
    PlayGuard.qml
    PlaylistPage.qml
    TrackListPage.qml
    TrackRow.qml
  RESOURCES
    icons/expand.svg
    icons/more.svg
    icons/music.svg
    icons/next.svg
    icons/pause.svg
    icons/previous.svg
    icons/queue.svg
    icons/repeat.svg
    icons/repeatOne.svg
    icons/shuffle.svg
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell Jfd.Ui.Library)
target_link_libraries(jfd_ui_music
  PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml jfd::music jfd::library jfd::jellyfin_api jfd_ui_shell jfd_ui_library
  PRIVATE jfd_ui_theme jfd_ui_controls)
target_compile_definitions(jfd_ui_music PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_music PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_music)
