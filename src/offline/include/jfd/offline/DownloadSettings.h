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
    // Play a downloaded copy from disk even while the server can be reached
    // (without the server it always is). Default on.
    inline constexpr QLatin1String PreferLocal{"downloads/preferLocal"};
    // How many downloads run at once, 1-3. Default 1.
    inline constexpr QLatin1String MaxParallel{"downloads/maxParallel"};
    // Unfinished downloads continue when the app starts; off, they start
    // paused. Default on.
    inline constexpr QLatin1String ResumeOnStart{"downloads/resumeOnStart"};
    // What "Download" makes by default: "original" (the file as it is), or
    // a compressed copy, "video-1080p" or "video-720p" (music then gets
    // "audio-opus128"). Default "original". See CompressedDownload.h.
    inline constexpr QLatin1String Quality{"downloads/quality"};
  } // namespace keys

  // <Videos>/Jellyfin (QStandardPaths::MoviesLocation): ~/Videos/Jellyfin
  // on Linux, %USERPROFILE%\Videos\Jellyfin on Windows.
  QString defaultDownloadFolder();

  // False if the section is already registered.
  bool registerDownloadSettings(settings::SettingsRegistry& registry);

} // namespace jfd::offline
