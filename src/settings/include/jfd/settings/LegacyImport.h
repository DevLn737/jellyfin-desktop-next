#pragma once

#include "jfd/settings/Settings.h"

#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

// Moving from the 2.x client (Jellyfin Media Player and its fork): its
// settings and the server it used. The sign-in token stays behind; it lives
// in the web client's browser storage, which is not read.
namespace jfd::settings {

  struct LegacyProfile {
    QString directory;
    // "section.key" as the 2.x client names them, from jellyfinmediaplayer.conf
    // and storage.json.
    QVariantMap values;
    // The server the 2.x client opened (main.userWebClient); empty if none.
    QString serverAddress;
  };

  // Where the 2.x client keeps its profile on this system, most likely first:
  // the 2.x folder, the 1.x "Jellyfin Media Player" folder and, on Linux, the
  // Flatpak's.
  QStringList legacyProfileDirectories();

  // Reads a 2.x profile folder; empty when it has no readable
  // jellyfinmediaplayer.conf. Nothing is written there.
  std::optional<LegacyProfile> readLegacyProfile(const QString& directory);

  // The first readable profile among the given folders.
  std::optional<LegacyProfile> findLegacyProfile(const QStringList& directories = legacyProfileDirectories());

  struct LegacyImportResult {
    // Full keys ("playback/subtitleSize") of the settings that took a 2.x
    // value, and of those whose 2.x value could not be used.
    QStringList imported;
    QStringList skipped;
  };

  // Copies every registered setting that names a legacyKey and has a value in
  // the profile, through its fromLegacy when it has one.
  LegacyImportResult importLegacySettings(Settings& settings, const LegacyProfile& profile);

} // namespace jfd::settings
