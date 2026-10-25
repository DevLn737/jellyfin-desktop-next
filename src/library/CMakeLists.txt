# Qt Gui for decoding and scaling images in the cache; no Qt Quick here, the
# screens (Jfd.Ui.Library) bind to these models.
find_package(Qt6 REQUIRED COMPONENTS Gui)

add_library(jfd_library STATIC
  src/CardImages.cpp
  src/CollectionIndex.cpp
  src/HomeModel.cpp
  src/ImageCache.cpp
  src/ItemBrowser.cpp
  src/ItemDetails.cpp
  src/ItemListModel.cpp
  src/LibraryModel.cpp
  src/LibraryUpdates.cpp
  src/SearchModel.cpp
  src/ServerStore.cpp
  src/SignIn.cpp
  include/jfd/library/CardImages.h
  include/jfd/library/CollectionIndex.h
  include/jfd/library/HomeModel.h
  include/jfd/library/ImageCache.h
  include/jfd/library/ItemBrowser.h
  include/jfd/library/ItemDetails.h
  include/jfd/library/ItemListModel.h
  include/jfd/library/LibraryModel.h
  include/jfd/library/LibraryUpdates.h
  include/jfd/library/SearchModel.h
  include/jfd/library/ServerStore.h
  include/jfd/library/SignIn.h)
add_library(jfd::library ALIAS jfd_library)
target_include_directories(jfd_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(jfd_library PUBLIC jfd::jellyfin_api jfd::platform Qt6::Core Qt6::Gui)
set_target_properties(jfd_library PROPERTIES AUTOMOC ON)

# Russian interface strings in i18n/library_ru.ts (see cmake/JfdTranslations.cmake).
jfd_add_translations(jfd_library)

# Type information for the screens' QML module, which declares these models
# as QML types (src/ui/library/include/jfd/ui/LibraryTypes.h).
qt_extract_metatypes(jfd_library)
