# The playback session wired into the app: fan-out of its state to the UI,
# the media integrations and diagnostics, and (with the Qt network modules
# and playback) the runtime on a signed-in server. Shared by the app and the headless
# SyncPlay agent.
add_library(jfd_session STATIC src/SessionHub.cpp)
add_library(jfd::session ALIAS jfd_session)
target_include_directories(jfd_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_session PUBLIC jfd::core)

if(TARGET jfd_playback)
  add_library(jfd_session_runtime STATIC
    src/ApiTransport.cpp
    src/GroupModel.cpp
    src/InputQueue.cpp
    src/PlaybackLauncher.cpp
    src/QtTimers.cpp
    src/SessionRuntime.cpp
    src/SleepDetector.cpp
    include/jfd/session/ApiTransport.h
    include/jfd/session/GroupModel.h
    include/jfd/session/InputQueue.h
    include/jfd/session/PlaybackLauncher.h
    include/jfd/session/QtTimers.h
    include/jfd/session/SessionRuntime.h
    include/jfd/session/SleepDetector.h
    include/jfd/session/SteadyClock.h)
  add_library(jfd::session_runtime ALIAS jfd_session_runtime)
  target_link_libraries(jfd_session_runtime PUBLIC jfd::session jfd::syncplay jfd::jellyfin_api jfd::playback Qt6::Core Qt6::Network)
  set_target_properties(jfd_session_runtime PROPERTIES AUTOMOC ON)
  # Russian strings of the group model in i18n/session_ru.ts.
  jfd_add_translations(jfd_session_runtime)
endif()
