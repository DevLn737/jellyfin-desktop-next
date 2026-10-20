import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// An on/off switch. Space toggles it.
T.Switch {
    id: control

    implicitWidth: 44
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    padding: 0

    indicator: Rectangle {
        x: control.leftPadding
        anchors.verticalCenter: parent.verticalCenter
        width: 44
        height: 24
        radius: height / 2
        color: control.checked ? Theme.accent : Theme.surfaceHover
        opacity: control.enabled ? 1 : 0.45
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            anchors.verticalCenter: parent.verticalCenter
            width: 18
            height: 18
            radius: 9
            color: Theme.text
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }
        }

        FocusFrame { shown: control.visualFocus; radius: height / 2 + Theme.focusRingWidth }
    }

    contentItem: Item {}
}
