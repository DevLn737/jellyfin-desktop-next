# The music player: the queue, and the playback session's music file with
# its own playback reports and the gapless hand-over to the next track. No
# screens (src/ui/music).
add_library(jfd_music STATIC
  src/MusicPlayer.cpp
  src/MusicQueue.cpp
  src/MusicQueueModel.cpp
  include/jfd/music/MusicPlayer.h
  include/jfd/music/MusicQueue.h
  include/jfd/music/MusicQueueModel.h)
add_library(jfd::music ALIAS jfd_music)
target_include_directories(jfd_music PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_music PUBLIC Qt6::Core jfd::session_runtime jfd::playback jfd::jellyfin_api jfd::core)
target_compile_definitions(jfd_music PUBLIC QT_NO_KEYWORDS)
set_target_properties(jfd_music PROPERTIES AUTOMOC ON)
