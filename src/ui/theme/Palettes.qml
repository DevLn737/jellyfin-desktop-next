pragma Singleton
import QtQuick

// Built-in colour palettes: maps from the names in Theme.qml to colours.
// Theme.colors is one of these (or a palette made from a file).
QtObject {
    readonly property var dark: ({
        background: "#101010",
        surface: "#1c1c1c",
        surfaceRaised: "#262626",
        surfaceHover: "#303030",
        border: "#3a3a3a",
        accent: "#00a4dc",
        accentHover: "#33b6e3",
        accentText: "#ffffff",
        text: "#f2f2f2",
        textSecondary: "#b3b3b3",
        textDisabled: "#6b6b6b",
        error: "#e5534b",
        success: "#3fb950",
        focusRing: "#00a4dc",
        scrim: "#b0000000",
        progress: "#00a4dc",
    })
}
