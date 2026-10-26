pragma Singleton
import QtQuick

// Colours for what is drawn over the video: the player's controls and the
// group's on-screen display. They stay dark and the same in every theme,
// because they sit on the picture, not on a page. Same names as Theme.
QtObject {
    readonly property color background: Palettes.dark.background
    readonly property color surface: Palettes.dark.surface
    readonly property color surfaceRaised: Palettes.dark.surfaceRaised
    readonly property color surfaceHover: Palettes.dark.surfaceHover
    readonly property color border: Palettes.dark.border
    readonly property color accent: Palettes.dark.accent
    readonly property color accentHover: Palettes.dark.accentHover
    readonly property color accentText: Palettes.dark.accentText
    readonly property color text: Palettes.dark.text
    readonly property color textSecondary: Palettes.dark.textSecondary
    readonly property color textDisabled: Palettes.dark.textDisabled
    readonly property color error: Palettes.dark.error
    readonly property color success: Palettes.dark.success
    readonly property color focusRing: Palettes.dark.focusRing
    readonly property color scrim: Palettes.dark.scrim
    readonly property color progress: Palettes.dark.progress
}
