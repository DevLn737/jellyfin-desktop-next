# Our client as the app runs it, without the window: headless mpv, the
# player screen's model and the watch-together model, driven like
# jfd_syncplay_agent (POSIX only: it reads stdin through a socket notifier).
if(NOT TARGET jfd_ui_player OR NOT TARGET jfd_media_backend)
  message(STATUS "Player modules not built: skipping the SyncPlay app agent")
  return()
endif()
add_executable(jfd_syncplay_app_agent appAgent.cpp)
target_link_libraries(jfd_syncplay_app_agent PRIVATE
  jfd_ui_player jfd::session_runtime jfd::media_backend Qt6::Gui nlohmann_json)
