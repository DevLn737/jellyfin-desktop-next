#pragma once

#include <jfd/settings/SettingsRegistry.h>

#include <QLatin1String>
#include <QString>

// The "Downloads" settings section: where downloads go and how much disk
// they may use.
namespace jfd::offline {

  namespace keys {
    // The folder downloads are kept in. Default: "Jellyfin" in the user's
    // Videos folder.
    inline constexpr QLatin1String Folder{"downloads/folder"};
    // Downloads use at most this many GiB; 0 means no limit. Default 100.
    inline constexpr QLatin1String MaxGiB{"downloads/maxGiB"};
    // A download does not start, and a running one pauses, when the disk
    // would have less than this many GiB free. Default 5.
    inline constexpr QLatin1String ReserveGiB{"downloads/reserveGiB"};
  } // namespace keys

  // <Videos>/Jellyfin (QStandardPaths::MoviesLocation): ~/Videos/Jellyfin
  // on Linux, %USERPROFILE%\Videos\Jellyfin on Windows.
  QString defaultDownloadFolder();

  // False if the section is already registered.
  bool registerDownloadSettings(settings::SettingsRegistry& registry);

} // namespace jfd::offline
