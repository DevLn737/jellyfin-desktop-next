# Headless SyncPlay client for checks against a live server (POSIX only: it
# multiplexes stdin with poll()).
add_executable(jfd_syncplay_agent main.cpp)
target_link_libraries(jfd_syncplay_agent PRIVATE jfd::core jfd::syncplay nlohmann_json)
