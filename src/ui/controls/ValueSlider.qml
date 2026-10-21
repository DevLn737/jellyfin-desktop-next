import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A horizontal slider. Left/Right move it by `stepSize` when focused.
T.Slider {
    id: control

    implicitWidth: 220
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    padding: 6

    background: Rectangle {
        x: control.leftPadding
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: control.availableWidth
        height: 4
        radius: 2
        color: Theme.surfaceHover

        Rectangle {
            width: control.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: Theme.accent
        }
    }

    handle: Rectangle {
        x: control.leftPadding + control.visualPosition * (control.availableWidth - width)
        y: control.topPadding + control.availableHeight / 2 - height / 2
        width: 18
        height: 18
        radius: 9
        color: control.pressed ? Theme.accentHover : Theme.text
        FocusFrame { shown: control.visualFocus; radius: 9 + Theme.focusRingWidth }
    }
}
