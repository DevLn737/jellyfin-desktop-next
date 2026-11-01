import QtQuick
import Jfd.Ui.Theme

// A grey placeholder while content loads. It pulses slowly, or stays still
// with simplified effects.
Rectangle {
    id: box
    // Colours of the nearest colour scope.
    readonly property var colors: Theme.colorsFor(box)

    implicitWidth: 120
    implicitHeight: 16
    radius: Theme.radius
    color: colors.surfaceRaised
    opacity: 0.6
    Accessible.ignored: true

    SequentialAnimation on opacity {
        running: box.visible && !Theme.reducedEffects
        loops: Animation.Infinite
        NumberAnimation { to: 1.0; duration: Theme.durationLong; easing.type: Easing.InOutSine }
        NumberAnimation { to: 0.6; duration: Theme.durationLong; easing.type: Easing.InOutSine }
    }
}
