import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The playback speed in the bar, e.g. "1.25×": plain text like the icon
// buttons around it, in the accent colour when the speed is not normal.
T.Button {
    id: control
    readonly property var colors: Theme.colorsFor(control)

    property string toolTipText
    property bool changed: false

    implicitWidth: Math.max(Theme.controlHeight, label.implicitWidth + Theme.spacing * 2)
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    Accessible.name: toolTipText

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Item {
        Text {
            id: label
            anchors.centerIn: parent
            text: control.text
            color: control.changed ? control.colors.accent : control.colors.text
            font.pixelSize: Theme.fontSizeBody
            font.weight: Theme.fontWeightBold
            style: Text.Outline
            styleColor: "#80000000"
        }
    }

    background: Rectangle {
        radius: height / 2
        color: control.down || control.hovered ? control.colors.surfaceHover : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        FocusFrame { shown: control.visualFocus; radius: height / 2 }
    }

    ToolTipLabel {
        text: control.toolTipText
        shown: control.hovered && control.toolTipText.length > 0
    }
}
