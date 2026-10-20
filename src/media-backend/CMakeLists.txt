# mpv playback and the Qt Quick video item. The adapter to core::MediaBackend
# follows once the core interfaces exist.
qt_add_library(jfd_media_backend STATIC
  MpvHandle.h
  MpvPlayer.cpp MpvPlayer.h
  MpvVideoItem.cpp MpvVideoItem.h)
add_library(jfd::media_backend ALIAS jfd_media_backend)
target_include_directories(jfd_media_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jfd_media_backend
  PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::OpenGL mpv::mpv
  PRIVATE jfd::diagnostics)
