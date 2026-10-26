import QtQuick
import Jfd.Ui.Theme

// The keyboard focus ring. Put it inside a control and bind `shown` to the
// control's visualFocus (focus gained by keyboard, not by mouse).
Rectangle {
    id: frame
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(frame)
    property bool shown: false
    property int inset: -Theme.focusRingWidth - 1

    anchors.fill: parent
    anchors.margins: inset
    radius: Theme.radius + Theme.focusRingWidth
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: colors.focusRing
    visible: shown
    z: 100
}
