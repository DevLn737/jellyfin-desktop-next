# The playback session wired into the app: fan-out of its state to the UI,
# the media integrations and diagnostics. Shared by the app and the headless
# SyncPlay agent.
add_library(jfd_session STATIC src/SessionHub.cpp)
add_library(jfd::session ALIAS jfd_session)
target_include_directories(jfd_session PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_session PUBLIC jfd::core)
