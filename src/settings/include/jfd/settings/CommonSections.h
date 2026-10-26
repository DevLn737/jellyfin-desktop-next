#pragma once

#include "jfd/settings/SettingsRegistry.h"

#include <QLatin1String>

#include <functional>

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

    // appearance
    // "dark", "light", "system" or "file:<name>" for a file in the themes
    // folder.
    inline constexpr QLatin1String Theme{"appearance/theme"};
    // A preset id ("blue", ...) or "custom".
    inline constexpr QLatin1String Accent{"appearance/accent"};
    // "#rrggbb", used when the accent is "custom".
    inline constexpr QLatin1String AccentCustom{"appearance/accentCustom"};
    // Shorter or no animations, for slow graphics.
    // "auto" (on with integrated or software graphics), "on", "off".
    // (Was interface/effects.)
    inline constexpr QLatin1String Effects{"appearance/effects"};
    inline constexpr QLatin1String OpenThemeFolder{"appearance/openThemeFolder"};
    inline constexpr QLatin1String ReloadThemes{"appearance/reloadThemes"};
    inline constexpr QLatin1String ExportTheme{"appearance/exportTheme"};

    // diagnostics
    // "error", "warning", "info" or "debug".
    inline constexpr QLatin1String LogLevel{"diagnostics/logLevel"};
    inline constexpr QLatin1String OpenLogFolder{"diagnostics/openLogFolder"};
    // Saves a zip with the logs, versions, GPU and session journal.
    inline constexpr QLatin1String SaveReport{"diagnostics/saveReport"};

    // window (hidden): restored at startup.
    // QWindow geometry in device-independent pixels, "x,y,width,height".
    inline constexpr QLatin1String WindowGeometry{"window/geometry"};
    // The screen the window was on (QScreen::name()).
    inline constexpr QLatin1String WindowScreen{"window/screen"};
    inline constexpr QLatin1String WindowMaximized{"window/maximized"};
    inline constexpr QLatin1String WindowFullScreen{"window/fullScreen"};
  } // namespace keys

  // The themes the user can pick besides the built-in ones, as choices
  // ("file:<name>" with the theme's name as `text`). Called whenever the
  // settings screen reads the list.
  using ThemeChoices = std::function<QList<Choice>()>;

  // Registers account, interface, appearance, diagnostics and window.
  // Returns false if any of them is already registered.
  bool registerCommonSections(SettingsRegistry& registry, ThemeChoices themeFiles = {});

} // namespace jfd::settings
