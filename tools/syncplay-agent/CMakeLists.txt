# Headless SyncPlay client for checks against a live server: the app's
# session runtime with a virtual file in place of mpv (POSIX only: it reads
# stdin through a socket notifier).
if(NOT TARGET jfd_session_runtime)
  message(STATUS "Qt network modules not built: skipping the SyncPlay agent")
  return()
endif()
add_executable(jfd_syncplay_agent main.cpp)
target_link_libraries(jfd_syncplay_agent PRIVATE jfd::session_runtime nlohmann_json)
