add_library(jfd_jellyfin_api STATIC
  src/ClientIdentity.cpp
  src/DeviceProfile.cpp
  src/JellyfinClient.cpp
  src/Models.cpp
  src/MusicApi.cpp
  src/Playback.cpp
  src/ServerDiscovery.cpp
  src/ServerTime.cpp
  src/ServerVersion.cpp
  src/SessionSocket.cpp
  src/SyncPlayApi.cpp
  include/jfd/api/ClientIdentity.h
  include/jfd/api/DeviceProfile.h
  include/jfd/api/JellyfinClient.h
  include/jfd/api/Models.h
  include/jfd/api/MusicApi.h
  include/jfd/api/Playback.h
  include/jfd/api/Result.h
  include/jfd/api/ServerDiscovery.h
  include/jfd/api/ServerTime.h
  include/jfd/api/ServerVersion.h
  include/jfd/api/SessionSocket.h
  include/jfd/api/SyncPlayApi.h)
add_library(jfd::jellyfin_api ALIAS jfd_jellyfin_api)
target_include_directories(jfd_jellyfin_api PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_jellyfin_api PUBLIC jfd::core Qt6::Core Qt6::Network Qt6::WebSockets)
set_target_properties(jfd_jellyfin_api PROPERTIES AUTOMOC ON)
target_compile_definitions(jfd_jellyfin_api PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
# MSVC reports C4702 (unreachable code) from inlined Qt string helpers at
# code generation, where /external:W0 no longer applies.
target_compile_options(jfd_jellyfin_api PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
