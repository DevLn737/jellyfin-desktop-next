import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A horizontal slider. Left/Right move it by `stepSize` when focused.
// Enter confirms the value (`accepted`, like Enter in a field) and goes no
// further, so it never also acts on the page around the slider.
T.Slider {
    id: control
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(control)

    signal accepted()
    // The line's thickness and the handle's size (the player's timeline is
    // larger in TV mode).
    property int trackHeight: 4
    property int handleSize: 18
    Keys.onReturnPressed: control.accepted()
    Keys.onEnterPressed: control.accepted()

    implicitWidth: 220
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    padding: 6

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: control.trackHeight
        radius: height / 2
        color: colors.surfaceHover

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: parent.radius
            color: colors.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.handleSize
        height: control.handleSize
        radius: control.handleSize / 2
        color: control.pressed ? colors.accentHover : colors.text
        FocusFrame { shown: control.visualFocus; radius: control.handleSize / 2 + Theme.focusRingWidth }
    }
}
