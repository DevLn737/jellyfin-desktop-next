#pragma once

#include <jfd/settings/SettingsRegistry.h>

#include <QLatin1String>

// The "Integrations" settings section: Discord and the update check.
namespace jfd::integrations {

  namespace keys {
    // Discord Rich Presence; off until the user turns it on.
    inline constexpr QLatin1String Discord{"integrations/discord"};
    // Ask GitHub for a newer release at startup and show its download page.
    inline constexpr QLatin1String CheckForUpdates{"integrations/checkForUpdates"};
    // "beta" (pre-releases too) or "stable".
    inline constexpr QLatin1String UpdateChannel{"integrations/updateChannel"};
  } // namespace keys

  // False if the section is already registered.
  bool registerIntegrationsSettings(settings::SettingsRegistry& registry);

} // namespace jfd::integrations
