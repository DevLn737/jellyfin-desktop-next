import QtQuick
import Jfd.Ui.Theme

// The keyboard focus ring. Put it inside a control and bind `shown` to the
// control's visualFocus (focus gained by keyboard or gamepad, not by
// mouse). A solid ring with a soft halo outside it, so it reads from the
// sofa on a bright poster as well as on the dark page.
Rectangle {
    id: frame
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(frame)
    property bool shown: false
    // Drawn inside the control's edge, halo inwards (a card in a row
    // that clips at its edges, where an outer ring would be cut off).
    property bool inside: false
    property color ringColor: colors.focusRing
    property int inset: inside ? 0 : -Theme.focusRingWidth - 1

    anchors.fill: parent
    anchors.margins: inset
    radius: Theme.radius + Theme.focusRingWidth
    color: "transparent"
    border.width: Theme.focusRingWidth
    border.color: frame.ringColor
    visible: shown
    z: 100

    Rectangle {
        objectName: "focusHalo"
        anchors.fill: parent
        anchors.margins: frame.inside ? Theme.focusRingWidth : -Theme.focusRingWidth
        radius: Math.max(0, frame.radius + (frame.inside ? -1 : 1) * Theme.focusRingWidth)
        color: "transparent"
        border.width: Theme.focusRingWidth
        border.color: colors.focusRing
        opacity: frame.inside ? 0.8 : 0.35
    }
}
