pragma Singleton
import QtQuick

// Colours for what is drawn over the video: the player's controls and the
// group's on-screen display. They stay dark and the same in every theme,
// because they sit on the picture, not on a page. Same names as Theme.
QtObject {
    // The dark palette, with the chosen accent (the application sets it);
    // also the `colorScope` of what is drawn over the video.
    property var colors: Palettes.dark

    readonly property color background: colors.background
    readonly property color surface: colors.surface
    readonly property color surfaceRaised: colors.surfaceRaised
    readonly property color surfaceHover: colors.surfaceHover
    readonly property color border: colors.border
    readonly property color accent: colors.accent
    readonly property color accentHover: colors.accentHover
    readonly property color accentText: colors.accentText
    readonly property color text: colors.text
    readonly property color textSecondary: colors.textSecondary
    readonly property color textDisabled: colors.textDisabled
    readonly property color error: colors.error
    readonly property color success: colors.success
    readonly property color focusRing: colors.focusRing
    readonly property color scrim: colors.scrim
    readonly property color progress: colors.progress
}
