import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A round button with only an icon. `toolTipText` is also the accessible name.
T.Button {
    id: control

    property url iconSource
    property string toolTipText

    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    Accessible.name: toolTipText

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            source: control.iconSource
            opacity: control.enabled ? 1 : 0.45
        }
    }

    background: Rectangle {
        radius: width / 2
        color: control.down || control.hovered ? Theme.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }

        FocusFrame { shown: control.visualFocus; radius: width / 2 }
    }

    ToolTipLabel {
        text: control.toolTipText
        shown: control.hovered && control.toolTipText.length > 0
    }
}
