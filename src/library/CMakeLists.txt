add_library(jfd_library STATIC
  src/ItemBrowser.cpp
  include/jfd/library/ItemBrowser.h)
add_library(jfd::library ALIAS jfd_library)
target_include_directories(jfd_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_library PUBLIC jfd::jellyfin_api Qt6::Core)
set_target_properties(jfd_library PROPERTIES AUTOMOC ON)
