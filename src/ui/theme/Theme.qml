pragma Singleton
import QtQuick

// Visual constants for every screen. Screens use these names instead of
// literal colours, sizes and durations, so the look changes in one place.
// The default palette follows Jellyfin Web's dark theme, which the group
// knows; the video player keeps its own, unchanging one (Overlay.qml).
QtObject {
    id: theme

    // Set by the application from the "Simplified effects" setting, which
    // by default turns on with integrated or software graphics: no blur,
    // and the durations below are halved.
    property bool reducedEffects: false
    // Blur (MultiEffect, layer effects) only when this is true.
    readonly property bool blurEnabled: !reducedEffects
    // Older name of reducedEffects.
    readonly property bool reduceMotion: reducedEffects

    // Colours come from a palette, a plain map of these names to colours
    // (Palettes.qml has the built-in ones). The names are shared by every
    // screen: they are only ever added, never renamed or removed.
    property var colors: Palettes.dark

    // Colours
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

    // Type: sizes in device-independent pixels; the interface scale setting
    // scales everything through QT_SCALE_FACTOR.
    readonly property string fontFamily: Qt.application.font.family
    readonly property int fontSizeSmall: 12
    readonly property int fontSizeBody: 14
    readonly property int fontSizeLarge: 17
    readonly property int fontSizeTitle: 22
    readonly property int fontSizeHeadline: 30
    readonly property int fontWeightNormal: Font.Normal
    readonly property int fontWeightBold: Font.DemiBold

    // Spacing on a 4 px grid
    readonly property int spacingTiny: 4
    readonly property int spacingSmall: 8
    readonly property int spacing: 12
    readonly property int spacingLarge: 20
    readonly property int spacingHuge: 32
    readonly property int radius: 6
    readonly property int radiusSmall: 3
    readonly property int focusRingWidth: 2

    // Sizes
    readonly property int controlHeight: 38
    readonly property int iconSize: 22
    readonly property int sidebarWidth: 220
    readonly property int sidebarCollapsedWidth: 64
    // Card widths; heights follow from the aspect ratios.
    readonly property int posterWidth: 160
    readonly property int thumbWidth: 280
    readonly property int squareWidth: 160
    readonly property real posterAspect: 2 / 3
    readonly property real thumbAspect: 16 / 9
    readonly property real squareAspect: 1
    // Card shapes: "poster" (2:3), "thumb" (16:9), "square" (album covers)
    // and "circle" (artists: a square image cut round).
    function cardWidthFor(shape) {
        return shape === "thumb" ? thumbWidth : (shape === "square" || shape === "circle") ? squareWidth : posterWidth
    }
    function cardAspectFor(shape) {
        return shape === "thumb" ? thumbAspect : (shape === "square" || shape === "circle") ? squareAspect : posterAspect
    }

    // Animation durations in milliseconds
    readonly property int durationFast: reducedEffects ? 60 : 120
    readonly property int durationNormal: reducedEffects ? 100 : 200
    readonly property int durationSlow: reducedEffects ? 160 : 320
    readonly property int easing: Easing.OutCubic
}
