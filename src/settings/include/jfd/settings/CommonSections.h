#pragma once

#include "jfd/settings/SettingsRegistry.h"

#include <QLatin1String>

// Sections that belong to the application as a whole. Playback, SyncPlay,
// Discord and library sections are registered by their own modules.
namespace jfd::settings {

  namespace keys {
    // account: actions handled by the sign-in flow.
    inline constexpr QLatin1String SignOut{"account/signOut"};
    inline constexpr QLatin1String ChangeServer{"account/changeServer"};

    // interface
    // "system", or a language code with a translation ("en", "ru").
    inline constexpr QLatin1String Language{"interface/language"};
    // Interface scale on top of the system's, 0.75 to 2.0.
    inline constexpr QLatin1String Scale{"interface/scale"};
    // Shorter or no animations, for slow graphics.
    // "auto" (on with integrated or software graphics), "on", "off".
    inline constexpr QLatin1String Effects{"interface/effects"};

    // diagnostics
    // "error", "warning", "info" or "debug".
    inline constexpr QLatin1String LogLevel{"diagnostics/logLevel"};
    inline constexpr QLatin1String OpenLogFolder{"diagnostics/openLogFolder"};

    // window (hidden): restored at startup.
    // QWindow geometry in device-independent pixels, "x,y,width,height".
    inline constexpr QLatin1String WindowGeometry{"window/geometry"};
    // The screen the window was on (QScreen::name()).
    inline constexpr QLatin1String WindowScreen{"window/screen"};
    inline constexpr QLatin1String WindowMaximized{"window/maximized"};
    inline constexpr QLatin1String WindowFullScreen{"window/fullScreen"};
  } // namespace keys

  // Registers account, interface, diagnostics and window. Returns false if
  // any of them is already registered.
  bool registerCommonSections(SettingsRegistry& registry);

} // namespace jfd::settings
