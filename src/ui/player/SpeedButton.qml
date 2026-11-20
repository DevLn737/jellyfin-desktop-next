import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// A labelled button of the player's bar: plain text like the icon buttons
// around it ("1.25×", "−10"), with an icon before it when one is set
// ("Episodes"). `changed` draws the text in the accent colour (a speed that
// is not normal).
T.Button {
    id: control
    readonly property var colors: Theme.colorsFor(control)

    property string toolTipText
    property bool changed: false
    property url iconSource

    implicitWidth: Math.max(Theme.controlHeight, content.implicitWidth + Theme.spacing * 2)
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    Accessible.name: toolTipText

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Item {
        Row {
            id: content
            anchors.centerIn: parent
            spacing: Theme.spacingSmall
            Icon {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.iconSource.toString().length > 0
                source: control.iconSource
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                visible: text.length > 0
                text: control.text
                color: control.changed ? control.colors.accent : control.colors.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Theme.fontWeightBold
                style: Text.Outline
                styleColor: "#80000000"
            }
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
