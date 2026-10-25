#pragma once

#include "AppServices.h"

#include <jfd/settings/LegacyImport.h>

#include <QPointer>
#include <QQuickWindow>

#include <optional>

// Moving from the 2.x client: on the first start (no settings file yet) its
// settings are imported and the sign-in page opens on its server. Settings >
// "Version 2.x" imports them again.
namespace jfd::desktop {

  struct LegacyImport {
    std::optional<settings::LegacyProfile> profile;
    // Settings taken at this start; -1 when nothing was imported.
    int imported = -1;
    // For the message after Import again (announceLegacyImport).
    QPointer<QQuickWindow> window;
  };

  // JFD_LEGACY_PROFILE names the folder instead of the usual places; the
  // smoke test looks nowhere else.
  LegacyImport findLegacyImport(bool smoke);

  // The "Version 2.x" section, only when a 2.x profile was found.
  bool registerLegacySettings(settings::SettingsRegistry& registry, const LegacyImport& legacy);

  // After the settings open; firstStart: the settings file did not exist.
  void importLegacyAtFirstStart(AppServices& services, LegacyImport& legacy, bool firstStart);

  // After installLibrary. connectServer: open the sign-in page on the 2.x
  // server (not in the smoke test, which stays offline).
  void installLegacy(AppServices& services, LegacyImport& legacy, bool connectServer);

  // After the window loads: says how many settings came over.
  void announceLegacyImport(LegacyImport& legacy, QQuickWindow& window);

} // namespace jfd::desktop
