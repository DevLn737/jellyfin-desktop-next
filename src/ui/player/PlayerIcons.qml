pragma Singleton
import QtQuick

// The player's own icons (white, 24×24 viewBox).
QtObject {
    readonly property url pause: Qt.resolvedUrl("icons/pause.svg")
    readonly property url replay: Qt.resolvedUrl("icons/replay.svg")
    readonly property url forward: Qt.resolvedUrl("icons/forward.svg")
    readonly property url next: Qt.resolvedUrl("icons/next.svg")
    readonly property url previous: Qt.resolvedUrl("icons/previous.svg")
    readonly property url subtitles: Qt.resolvedUrl("icons/subtitles.svg")
    readonly property url audio: Qt.resolvedUrl("icons/audio.svg")
    readonly property url fullscreen: Qt.resolvedUrl("icons/fullscreen.svg")
    readonly property url fullscreenExit: Qt.resolvedUrl("icons/fullscreenExit.svg")
    readonly property url tune: Qt.resolvedUrl("icons/tune.svg")
    readonly property url episodes: Qt.resolvedUrl("icons/episodes.svg")
    readonly property url volume: Qt.resolvedUrl("icons/volume.svg")
    readonly property url volumeOff: Qt.resolvedUrl("icons/volumeOff.svg")
}
