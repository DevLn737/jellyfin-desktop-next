# Jfd.Ui.Player: the player screen. Video from mpv under an on-screen
# display (timeline, play/pause, jumps, next, audio and subtitle menus,
# subtitle and audio delay, skip intro and credits, the group indicator).
# PlayerSession (PlayerModel) runs the track table, media segments and
# playback reports for the file the session plays.
qt_add_library(jfd_ui_player STATIC
  src/HdrOutput.cpp
  src/PlayerModel.cpp
  include/jfd/ui/HdrOutput.h
  include/jfd/ui/PlayerModel.h
  include/jfd/ui/PlayerSession.h)
target_include_directories(jfd_ui_player PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# The generated QML type registration includes the headers by file name.
target_include_directories(jfd_ui_player PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include/jfd/ui)
set_source_files_properties(PlayerIcons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(jfd_ui_player
  URI Jfd.Ui.Player
  VERSION 1.0
  QML_FILES
    EpisodesPanel.qml
    HdrScene.qml
    PlayerIcons.qml
    PlayerPage.qml
    TrackMenu.qml
    TuneMenu.qml
  RESOURCES
    icons/audio.svg
    icons/episodes.svg
    icons/forward.svg
    icons/fullscreen.svg
    icons/fullscreenExit.svg
    icons/next.svg
    icons/pause.svg
    icons/previous.svg
    icons/replay.svg
    icons/subtitles.svg
    icons/tune.svg
    icons/volume.svg
    icons/volumeOff.svg
  DEPENDENCIES Jfd.Ui.Theme Jfd.Ui.Controls Jfd.Ui.Shell Jfd.Ui.Group)
target_link_libraries(jfd_ui_player
  PUBLIC Qt6::Core Qt6::Quick Qt6::Qml jfd::session_runtime jfd::playback jfd::media_backend
  PRIVATE jfd_ui_theme jfd_ui_controls jfd_ui_shell jfd_ui_group jfd_ui_library)
target_compile_definitions(jfd_ui_player PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
target_compile_options(jfd_ui_player PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
jfd_add_translations(jfd_ui_player)

# HDR10 output (HdrOutput): Windows with the Direct3D 11 video item, and the
# shader that encodes the interface to PQ. Without either the setting stays
# hidden and the window SDR.
get_target_property(_jfd_media_defs jfd_media_backend INTERFACE_COMPILE_DEFINITIONS)
find_package(Qt6 QUIET COMPONENTS ShaderTools)
if(WIN32 AND "JFD_HAVE_D3D11_VIDEO=1" IN_LIST _jfd_media_defs AND Qt6ShaderTools_FOUND)
  qt_add_shaders(jfd_ui_player "jfd_ui_player_shaders"
    PREFIX "/qt/qml/Jfd/Ui/Player"
    FILES shaders/hdr10_ui.frag)
  # QRhi (<rhi/qrhi.h>) for the swap chain's format and HDR info.
  find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
  target_link_libraries(jfd_ui_player PRIVATE Qt6::GuiPrivate)
  target_compile_definitions(jfd_ui_player PRIVATE JFD_HDR_OUTPUT=1)
  message(STATUS "Player: HDR10 output available")
elseif(WIN32 AND JFD_ENFORCE_PINS)
  message(FATAL_ERROR "HDR10 output needs the Direct3D 11 video item and Qt Shader Tools")
endif()
