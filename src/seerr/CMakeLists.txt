# Seerr (Overseerr/Jellyseerr successor) client without UI: sign-in as the
# user's own Jellyfin account, browse, search, titles, requests and their
# statuses. Qt Core and Network only. See docs/seerr.md.
add_library(jfd_seerr STATIC
  src/Models.cpp
  src/RequestListModel.cpp
  src/SeerrClient.cpp
  src/SeerrService.cpp
  src/SessionStore.cpp
  src/TitleDetailsModel.cpp
  src/TitleListModel.cpp
  include/jfd/seerr/Models.h
  include/jfd/seerr/RequestListModel.h
  include/jfd/seerr/SeerrClient.h
  include/jfd/seerr/SeerrService.h
  include/jfd/seerr/SessionStore.h
  include/jfd/seerr/TitleDetailsModel.h
  include/jfd/seerr/TitleListModel.h)
add_library(jfd::seerr ALIAS jfd_seerr)
target_include_directories(jfd_seerr PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_seerr PUBLIC jfd::jellyfin_api jfd::platform Qt6::Core Qt6::Network)
set_target_properties(jfd_seerr PROPERTIES AUTOMOC ON)
target_compile_definitions(jfd_seerr PUBLIC QT_NO_KEYWORDS PRIVATE QT_NO_CAST_FROM_ASCII)
