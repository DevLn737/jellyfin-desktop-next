# Only the credential store so far; the other platform services (power,
# media keys, displays) join this target as they are written.
add_library(jfd_platform STATIC
  src/CredentialStore.cpp
  include/jfd/platform/CredentialStore.h)
add_library(jfd::platform ALIAS jfd_platform)
target_include_directories(jfd_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_platform PUBLIC Qt6::Core)
target_compile_definitions(jfd_platform PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)

if(WIN32)
  target_sources(jfd_platform PRIVATE src/CredentialStoreWindows.cpp)
  target_link_libraries(jfd_platform PRIVATE advapi32)
else()
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(LIBSECRET IMPORTED_TARGET libsecret-1)
  endif()
  if(NOT APPLE AND LIBSECRET_FOUND)
    target_sources(jfd_platform PRIVATE src/CredentialStoreSecretService.cpp)
    target_link_libraries(jfd_platform PRIVATE PkgConfig::LIBSECRET)
  else()
    message(STATUS "No credential store for this platform: tokens are kept in memory only")
    target_sources(jfd_platform PRIVATE src/CredentialStoreNone.cpp)
  endif()
endif()
