# The account: the connection to one Jellyfin server as one user (client,
# session socket, saved servers, tokens in the system credential store), the
# way in (SignIn) and the user scope other modules hang their per-user state
# on. No Qt Quick: the screens, the session runtime and headless tools share
# it.
add_library(jfd_account STATIC
  src/Account.cpp
  src/ServerStore.cpp
  src/SignIn.cpp
  src/UserScope.cpp
  include/jfd/account/Account.h
  include/jfd/account/ServerStore.h
  include/jfd/account/SignIn.h
  include/jfd/account/UserScope.h)
add_library(jfd::account ALIAS jfd_account)
target_include_directories(jfd_account PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_account PUBLIC jfd::jellyfin_api jfd::platform Qt6::Core)
set_target_properties(jfd_account PROPERTIES AUTOMOC ON)

# Russian interface strings in i18n/account_ru.ts (see cmake/JfdTranslations.cmake).
jfd_add_translations(jfd_account)

# Type information for the login screen's QML module, which declares SignIn
# as a QML type (src/ui/library/include/jfd/ui/LibraryTypes.h).
qt_extract_metatypes(jfd_account)
