# Seerr (Overseerr/Jellyseerr successor) client without UI: sign-in as the
# user's own Jellyfin account, browse, search, titles, requests and their
# statuses. Qt Core and Network only. See docs/seerr.md.
add_library(jfd_seerr STATIC
  src/Models.cpp
  src/SeerrClient.cpp
  src/SessionStore.cpp
  include/jfd/seerr/Models.h
  include/jfd/seerr/SeerrClient.h
  include/jfd/seerr/SessionStore.h)
add_library(jfd::seerr ALIAS jfd_seerr)
target_include_directories(jfd_seerr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_seerr PUBLIC jfd::jellyfin_api jfd::platform Qt6::Core Qt6::Network)
set_target_properties(jfd_seerr PROPERTIES AUTOMOC ON)
target_compile_definitions(jfd_seerr PUBLIC QT_NO_KEYWORDS PRIVATE QT_NO_CAST_FROM_ASCII)
