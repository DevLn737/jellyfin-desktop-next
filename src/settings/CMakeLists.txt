# Settings schema, registry and storage. A leaf module: Qt Core only, so
# every other module can register its section.
add_library(jfd_settings STATIC
  src/CommonSections.cpp
  src/Schema.cpp
  src/Settings.cpp
  src/SettingsRegistry.cpp
  include/jfd/settings/CommonSections.h
  include/jfd/settings/Schema.h
  include/jfd/settings/Settings.h
  include/jfd/settings/SettingsRegistry.h)
add_library(jfd::settings ALIAS jfd_settings)
target_include_directories(jfd_settings PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_settings PUBLIC Qt6::Core)
set_target_properties(jfd_settings PROPERTIES AUTOMOC ON)
target_compile_definitions(jfd_settings PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
jfd_add_translations(jfd_settings)
# See src/jellyfin-api/CMakeLists.txt: C4702 from inlined Qt headers.
target_compile_options(jfd_settings PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
