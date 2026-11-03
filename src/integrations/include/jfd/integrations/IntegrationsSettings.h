#pragma once

#include <jfd/settings/SettingsRegistry.h>

#include <QLatin1String>

// The "Integrations" settings section: Discord and the update check.
namespace jfd::integrations {

  namespace keys {
    // Discord Rich Presence; off until the user turns it on.
    inline constexpr QLatin1String Discord{"integrations/discord"};
    // The poster on the Discord card. Discord fetches it from the server's
    // public address, which the card then shows to Discord friends: off
    // until the user opts in.
    inline constexpr QLatin1String DiscordPoster{"integrations/discordPoster"};
    // Ask GitHub for a newer release at startup; download and verify it, and
    // install it when the user restarts from the notice (or show the
    // download page where the build cannot update itself).
    inline constexpr QLatin1String CheckForUpdates{"integrations/checkForUpdates"};
    // "beta" (pre-releases too) or "stable".
    inline constexpr QLatin1String UpdateChannel{"integrations/updateChannel"};
  } // namespace keys

  // False if the section is already registered.
  bool registerIntegrationsSettings(settings::SettingsRegistry& registry);

} // namespace jfd::integrations
