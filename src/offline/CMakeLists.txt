# Offline downloads (stage 3, package 3.10a): the download folder and its
# manifests, the catalog built from them, the space limits and the queue
# with its worker thread; playing copies through the launcher's LocalSource,
# the offline watch journal and its sync with the server. Qt Core and Network only; the screens come later
# in src/ui/library.
add_library(jfd_offline STATIC
  src/DownloadQueue.cpp
  src/DownloadSettings.cpp
  src/DownloadStore.cpp
  src/DownloadWorker.cpp
  src/ExtrasDownloader.cpp
  src/JournalReportSink.cpp
  src/Manifest.cpp
  src/OfflineCatalog.cpp
  src/OfflineLocalSource.cpp
  src/OfflineSync.cpp
  src/StorageGuard.cpp
  src/ViewJournal.cpp
  include/jfd/offline/DownloadQueue.h
  include/jfd/offline/DownloadSettings.h
  include/jfd/offline/DownloadStore.h
  include/jfd/offline/DownloadWorker.h
  include/jfd/offline/ExtrasDownloader.h
  include/jfd/offline/JournalReportSink.h
  include/jfd/offline/Manifest.h
  include/jfd/offline/OfflineCatalog.h
  include/jfd/offline/OfflineLocalSource.h
  include/jfd/offline/OfflineSync.h
  include/jfd/offline/StorageGuard.h
  include/jfd/offline/ViewJournal.h)
add_library(jfd::offline ALIAS jfd_offline)
target_include_directories(jfd_offline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# jfd::session for the LocalSource interface only (a header); jfd::playback
# for the report sink and the player's path rule.
target_link_libraries(jfd_offline PUBLIC Qt6::Core Qt6::Network jfd::settings jfd::jellyfin_api
  jfd::playback jfd::session)
set_target_properties(jfd_offline PROPERTIES AUTOMOC ON)
target_compile_definitions(jfd_offline PUBLIC QT_NO_KEYWORDS QT_NO_CAST_FROM_ASCII)
# See src/jellyfin-api/CMakeLists.txt: C4702 from inlined Qt headers.
target_compile_options(jfd_offline PUBLIC $<$<CXX_COMPILER_ID:MSVC>:/wd4702>)
# The settings section's strings, in i18n/offline_ru.ts.
jfd_add_translations(jfd_offline)
