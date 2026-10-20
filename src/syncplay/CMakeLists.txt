add_library(jfd_syncplay STATIC
  src/GroupClient.cpp
  src/Protocol.cpp
  src/TimeSync.cpp)
add_library(jfd::syncplay ALIAS jfd_syncplay)
target_include_directories(jfd_syncplay PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_syncplay PUBLIC jfd::core PRIVATE nlohmann_json)
