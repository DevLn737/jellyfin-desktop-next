# Integrations with other applications: Discord Rich Presence and the check
# for a newer release. Qt Core and Network only; the application feeds them
# from the session (see docs) and the settings.
add_library(jfd_integrations STATIC
  src/DiscordIpc.cpp
  src/DiscordActivity.cpp
  src/DiscordPresence.cpp
  src/UpdateChecker.cpp
  include/jfd/integrations/DiscordIpc.h
  include/jfd/integrations/DiscordActivity.h
  include/jfd/integrations/DiscordPresence.h
  include/jfd/integrations/UpdateChecker.h)
add_library(jfd::integrations ALIAS jfd_integrations)
target_include_directories(jfd_integrations PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_integrations PUBLIC Qt6::Core Qt6::Network)
target_compile_definitions(jfd_integrations PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
set_target_properties(jfd_integrations PROPERTIES AUTOMOC ON)
jfd_add_translations(jfd_integrations)
