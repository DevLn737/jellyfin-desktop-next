pragma Singleton
import QtQuick

// The music screens' icons (white, 24×24 viewBox); the ones the controls
// ship (play, check, close) come from Jfd.Ui.Controls Icons.
QtObject {
    readonly property url music: Qt.resolvedUrl("icons/music.svg")
    readonly property url pause: Qt.resolvedUrl("icons/pause.svg")
    readonly property url next: Qt.resolvedUrl("icons/next.svg")
    readonly property url previous: Qt.resolvedUrl("icons/previous.svg")
    readonly property url shuffle: Qt.resolvedUrl("icons/shuffle.svg")
    readonly property url repeat: Qt.resolvedUrl("icons/repeat.svg")
    readonly property url repeatOne: Qt.resolvedUrl("icons/repeatOne.svg")
    readonly property url queue: Qt.resolvedUrl("icons/queue.svg")
    readonly property url more: Qt.resolvedUrl("icons/more.svg")
    readonly property url expand: Qt.resolvedUrl("icons/expand.svg")
}
