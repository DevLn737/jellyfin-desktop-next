# Integrations with other applications: Discord Rich Presence, the check for a
# newer release, the bridge that feeds the session to the system media
# controls, Discord and sleep inhibition, and their settings section. Qt Core
# and Network only.
add_library(jfd_integrations STATIC
  src/DiscordIpc.cpp
  src/DiscordActivity.cpp
  src/DiscordPresence.cpp
  src/UpdateChecker.cpp
  src/MediaSessionBridge.cpp
  src/IntegrationsSettings.cpp
  src/NowPlayingItem.cpp
  include/jfd/integrations/DiscordIpc.h
  include/jfd/integrations/DiscordActivity.h
  include/jfd/integrations/DiscordPresence.h
  include/jfd/integrations/UpdateChecker.h
  include/jfd/integrations/MediaSessionBridge.h
  include/jfd/integrations/IntegrationsSettings.h
  include/jfd/integrations/NowPlayingItem.h)
add_library(jfd::integrations ALIAS jfd_integrations)
target_include_directories(jfd_integrations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_integrations PUBLIC Qt6::Core Qt6::Network jfd::core jfd::platform
  jfd::settings jfd::jellyfin_api)
target_compile_definitions(jfd_integrations PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
set_target_properties(jfd_integrations PROPERTIES AUTOMOC ON)
jfd_add_translations(jfd_integrations)
