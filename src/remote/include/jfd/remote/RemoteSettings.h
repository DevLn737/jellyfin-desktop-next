#pragma once

#include <jfd/settings/SettingsRegistry.h>

#include <QLatin1String>

// The "Remote control" settings section.
namespace jfd::remote {

  namespace keys {
    // Other Jellyfin apps of the same user ("Play on" in Jellyfin Web, the
    // phone apps) may control this client through the server. On by
    // default, as in Jellyfin Web.
    inline constexpr QLatin1String AllowServerControl{"remote/allowServerControl"};
  } // namespace keys

  // False if the section is already registered.
  bool registerRemoteSettings(settings::SettingsRegistry& registry);

} // namespace jfd::remote
