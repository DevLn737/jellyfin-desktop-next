import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The player bar's speed button, as small as the icon buttons around it.
// At normal speed it is the speedometer icon. At another speed the
// speedometer's rim holds the speed itself ("1.5"), in the accent colour,
// instead of a needle and a label beside it. `text` stays the full speed
// ("1.5×") for screen readers and the tooltip.
T.Button {
    id: control
    readonly property var colors: Theme.colorsFor(control)

    property string toolTipText
    property bool changed: false
    property url iconSource
    property url gaugeSource
    // The number drawn in the gauge.
    property string value: text.replace("×", "")

    readonly property int gaugeSize: Theme.iconSize + 6

    implicitWidth: Theme.controlHeight
    implicitHeight: Theme.controlHeight
    focusPolicy: Qt.StrongFocus
    Accessible.name: text.length > 0 ? toolTipText + " " + text : toolTipText

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    contentItem: Item {
        Icon {
            anchors.centerIn: parent
            visible: !control.changed
            source: control.iconSource
        }
        Item {
            objectName: "speedGauge"
            anchors.centerIn: parent
            width: control.gaugeSize
            height: control.gaugeSize
            visible: control.changed

            Icon {
                anchors.fill: parent
                size: control.gaugeSize
                source: control.gaugeSource
            }
            Text {
                objectName: "speedValue"
                // The room inside the rim.
                readonly property real room: parent.width * 0.72
                // Inside the rim: its centre is a little above the middle.
                anchors.horizontalCenter: parent.horizontalCenter
                y: parent.height * 0.565 - height / 2
                text: control.value
                color: control.colors.accent
                font.pixelSize: Math.round(control.gaugeSize * (control.value.length > 3 ? 0.34 : 0.4))
                font.weight: Theme.fontWeightBold
                font.letterSpacing: -0.3
                // Any font's width fits: shrunk when it is wider than the room.
                scale: implicitWidth > room ? room / implicitWidth : 1
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
        text: control.text.length > 0 ? control.toolTipText + " " + control.text : control.toolTipText
        shown: control.hovered && control.toolTipText.length > 0
    }
}
