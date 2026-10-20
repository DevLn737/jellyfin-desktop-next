# mpv playback and the Qt Quick video items. The adapter to core::MediaBackend
# follows once the core interfaces exist.
qt_add_library(jfd_media_backend STATIC
  MpvHandle.h
  MpvPlayer.cpp MpvPlayer.h
  MpvVideoItem.cpp MpvVideoItem.h
  RepaintRequest.h)
add_library(jfd::media_backend ALIAS jfd_media_backend)
target_include_directories(jfd_media_backend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(jfd_media_backend
  PUBLIC Qt6::Core Qt6::Gui Qt6::Quick Qt6::OpenGL mpv::mpv
  PRIVATE jfd::diagnostics)

# Windows: the Direct3D 11 video item needs libmpv's D3D11 render API (client
# API 2.6, render_d3d11.h). The pinned libmpv has it; another libmpv without
# the header builds with the OpenGL item only.
if(WIN32)
  if(MPV_INCLUDE_DIR AND EXISTS "${MPV_INCLUDE_DIR}/mpv/render_d3d11.h")
    target_sources(jfd_media_backend PRIVATE MpvD3D11VideoItem.cpp MpvD3D11VideoItem.h)
    target_compile_definitions(jfd_media_backend PUBLIC JFD_HAVE_D3D11_VIDEO=1)
    target_link_libraries(jfd_media_backend PRIVATE d3d11 dxgi)
    message(STATUS "Video: OpenGL and Direct3D 11")
  elseif(JFD_ENFORCE_PINS)
    message(FATAL_ERROR "The pinned libmpv must provide mpv/render_d3d11.h (scripts/fetch-windows-deps.ps1)")
  else()
    message(STATUS "Video: OpenGL only (libmpv without render_d3d11.h)")
  endif()
endif()
