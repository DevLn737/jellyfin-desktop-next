# Integrations with other applications: Discord Rich Presence, the check for a
# newer release, the bridge that feeds the session to the system media
# controls, Discord and sleep inhibition, the release signature check, and
# their settings section. Qt Core and Network only.
# The minisign public key release files are signed with ("RW..." line of the
# .pub file). Without it the client checks for updates but installs none.
set(JFD_RELEASE_PUBLIC_KEY "" CACHE STRING "minisign public key of the release files")
if(NOT JFD_RELEASE_PUBLIC_KEY STREQUAL "" AND NOT JFD_RELEASE_PUBLIC_KEY MATCHES "^RW[A-Za-z0-9+/]+$")
  message(FATAL_ERROR "JFD_RELEASE_PUBLIC_KEY is not a minisign public key: ${JFD_RELEASE_PUBLIC_KEY}")
endif()
configure_file(src/ReleaseKey.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/ReleaseKey.h @ONLY)

# Monocypher (third_party/monocypher): Ed25519 and BLAKE2b for minisign. Built
# without the project's warning flags; it is vendored unchanged.
enable_language(C)
add_library(jfd_monocypher STATIC
  ${PROJECT_SOURCE_DIR}/third_party/monocypher/monocypher.c
  ${PROJECT_SOURCE_DIR}/third_party/monocypher/monocypher-ed25519.c)
set_target_properties(jfd_monocypher PROPERTIES COMPILE_OPTIONS "" C_STANDARD 99)
target_include_directories(jfd_monocypher SYSTEM PUBLIC ${PROJECT_SOURCE_DIR}/third_party/monocypher)

add_library(jfd_integrations STATIC
  src/DiscordIpc.cpp
  src/DiscordActivity.cpp
  src/DiscordPresence.cpp
  src/UpdateChecker.cpp
  src/MediaSessionBridge.cpp
  src/IntegrationsSettings.cpp
  src/NowPlayingItem.cpp
  src/ReleaseSignature.cpp
  src/AppImageUpdater.cpp
  include/jfd/integrations/DiscordIpc.h
  include/jfd/integrations/DiscordActivity.h
  include/jfd/integrations/DiscordPresence.h
  include/jfd/integrations/UpdateChecker.h
  include/jfd/integrations/MediaSessionBridge.h
  include/jfd/integrations/IntegrationsSettings.h
  include/jfd/integrations/NowPlayingItem.h
  include/jfd/integrations/ReleaseSignature.h
  include/jfd/integrations/AppImageUpdater.h)
add_library(jfd::integrations ALIAS jfd_integrations)
target_include_directories(jfd_integrations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_integrations PUBLIC Qt6::Core Qt6::Network jfd::core jfd::platform
  jfd::settings jfd::jellyfin_api)
target_link_libraries(jfd_integrations PRIVATE jfd_monocypher)
target_include_directories(jfd_integrations PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(jfd_integrations PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
set_target_properties(jfd_integrations PROPERTIES AUTOMOC ON)
jfd_add_translations(jfd_integrations)
