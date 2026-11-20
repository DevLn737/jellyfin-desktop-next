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
    // "Hide for now" (also in the player's settings menu): nothing is shown
    // until it is turned off again.
    inline constexpr QLatin1String DiscordHidden{"integrations/discordHidden"};
    // "full": the title, poster and links; "kind": only "Watching a movie".
    inline constexpr QLatin1String DiscordDetail{"integrations/discordDetail"};
    // What may be shown, by kind (hidden here; set on the "What to show in
    // Discord" page).
    inline constexpr QLatin1String DiscordMovies{"integrations/discordMovies"};
    inline constexpr QLatin1String DiscordSeries{"integrations/discordSeries"};
    inline constexpr QLatin1String DiscordMusic{"integrations/discordMusic"};
    inline constexpr QLatin1String DiscordLiveTv{"integrations/discordLiveTv"};
    // Ids of the libraries never shown, separated by commas (hidden; set
    // on that page).
    inline constexpr QLatin1String DiscordHiddenLibraries{"integrations/discordHiddenLibraries"};
    // Action: opens the "What to show in Discord" page.
    inline constexpr QLatin1String DiscordChoose{"integrations/discordChoose"};
    // Ask GitHub for a newer release at startup; download and verify it, and
    // install it when the user restarts from the notice (or show the
    // download page where the build cannot update itself).
    inline constexpr QLatin1String CheckForUpdates{"integrations/checkForUpdates"};
    // "beta" (pre-releases too) or "stable".
    inline constexpr QLatin1String UpdateChannel{"integrations/updateChannel"};
    // Action: asks for a newer release now and says what came back (the
    // startup check stays silent when there is none or it fails).
    inline constexpr QLatin1String CheckNow{"integrations/checkNow"};
  } // namespace keys

  // False if the section is already registered.
  bool registerIntegrationsSettings(settings::SettingsRegistry& registry);

} // namespace jfd::integrations
