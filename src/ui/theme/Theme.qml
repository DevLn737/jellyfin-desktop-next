pragma Singleton
import QtQuick

// Visual constants for every screen. Screens use these names instead of
// literal colours, sizes and durations, so the look changes in one place.
// The palette follows Jellyfin Web's dark theme, which the group knows.
QtObject {
    id: theme

    // Set by the application from the "Reduce animations" setting, and on
    // weak graphics. Durations below shrink to a fraction or to zero.
    property bool reduceMotion: false

    // Colours
    readonly property color background: "#101010"
    readonly property color surface: "#1c1c1c"
    readonly property color surfaceRaised: "#262626"
    readonly property color surfaceHover: "#303030"
    readonly property color border: "#3a3a3a"
    readonly property color accent: "#00a4dc"
    readonly property color accentHover: "#33b6e3"
    readonly property color accentText: "#ffffff"
    readonly property color text: "#f2f2f2"
    readonly property color textSecondary: "#b3b3b3"
    readonly property color textDisabled: "#6b6b6b"
    readonly property color error: "#e5534b"
    readonly property color success: "#3fb950"
    readonly property color focusRing: "#00a4dc"
    readonly property color scrim: "#b0000000"
    readonly property color progress: "#00a4dc"

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

    // Animation durations in milliseconds
    readonly property int durationFast: reduceMotion ? 0 : 120
    readonly property int durationNormal: reduceMotion ? 0 : 200
    readonly property int durationSlow: reduceMotion ? 0 : 320
    readonly property int easing: Easing.OutCubic
}
