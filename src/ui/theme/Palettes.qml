pragma Singleton
import QtQuick

// Built-in colour palettes: maps from the names in Theme.qml to colours.
// Theme.colors is one of these (or one made from the accent setting or a
// theme file). The same values are made in C++ by darkPalette(),
// lightPalette() and depthPalette() (ThemePalette.cpp); a test keeps the two equal.
QtObject {
    // "Depth", the default look. Beside the shared names it has glass and
    // main-button colours (Theme.qml has fallbacks for palettes without them).
    readonly property var depth: ({
        background: "#0a0e16",
        surface: "#111826",
        surfaceRaised: "#1a2232",
        surfaceHover: "#232e42",
        border: "#14ffffff",
        accent: "#6fa8b3",
        accentHover: "#8ab8c1",
        accentText: "#000000",
        text: "#e8ecf2",
        textSecondary: "#aab3c1",
        textDisabled: "#6f7a8c",
        error: "#e5534b",
        success: "#3fb950",
        focusRing: "#6fa8b3",
        scrim: "#b0060910",
        progress: "#6fa8b3",
        glassFill: "#80101622",
        glassBorder: "#14ffffff",
        primary: "#e8ecf2",
        primaryText: "#0a0e16",
    })

    readonly property var dark: ({
        background: "#101010",
        surface: "#1c1c1c",
        surfaceRaised: "#262626",
        surfaceHover: "#303030",
        border: "#3a3a3a",
        accent: "#00a4dc",
        accentHover: "#06c0ff",
        accentText: "#000000",
        text: "#f2f2f2",
        textSecondary: "#b3b3b3",
        textDisabled: "#6b6b6b",
        error: "#e5534b",
        success: "#3fb950",
        focusRing: "#00a4dc",
        scrim: "#b0000000",
        progress: "#00a4dc",
    })

    readonly property var light: ({
        background: "#f4f5f7",
        surface: "#ffffff",
        surfaceRaised: "#eceef1",
        surfaceHover: "#e0e3e8",
        border: "#cfd3da",
        accent: "#00a4dc",
        accentHover: "#0089b8",
        accentText: "#000000",
        text: "#16181c",
        textSecondary: "#566070",
        textDisabled: "#9aa1ad",
        error: "#c62828",
        success: "#1b7f3b",
        focusRing: "#0091c3",
        scrim: "#b0000000",
        progress: "#0091c3",
    })
}
