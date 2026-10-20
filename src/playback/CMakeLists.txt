# Playback logic between the session and the server: which mpv track is which
# Jellyfin stream, playback reports, media segments. No UI, no mpv.
add_library(jfd_playback STATIC
  src/PlaybackReporter.cpp
  src/StreamPlan.cpp
  src/TrackTable.cpp
  include/jfd/playback/PlaybackReporter.h
  include/jfd/playback/StreamPlan.h
  include/jfd/playback/TrackTable.h)
add_library(jfd::playback ALIAS jfd_playback)
target_include_directories(jfd_playback PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_playback PUBLIC jfd::jellyfin_api jfd::core Qt6::Core)
set_target_properties(jfd_playback PROPERTIES AUTOMOC ON)
