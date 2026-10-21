pragma Singleton
import QtQuick

// Icon files shipped with the controls (white, 24×24 viewBox). Use with
// Icon or ActionButton.iconSource.
QtObject {
    readonly property url home: Qt.resolvedUrl("icons/home.svg")
    readonly property url search: Qt.resolvedUrl("icons/search.svg")
    readonly property url group: Qt.resolvedUrl("icons/group.svg")
    readonly property url settings: Qt.resolvedUrl("icons/settings.svg")
    readonly property url back: Qt.resolvedUrl("icons/back.svg")
    readonly property url close: Qt.resolvedUrl("icons/close.svg")
    readonly property url play: Qt.resolvedUrl("icons/play.svg")
    readonly property url check: Qt.resolvedUrl("icons/check.svg")
    readonly property url error: Qt.resolvedUrl("icons/error.svg")
    readonly property url library: Qt.resolvedUrl("icons/library.svg")
    readonly property url menu: Qt.resolvedUrl("icons/menu.svg")
}
