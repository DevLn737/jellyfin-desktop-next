import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Player

// Speed (alone; a group's speed is on the group page), quality, subtitle
// and audio delay, and automatic skipping of intros and credits.
Rectangle {
    id: menu

    width: 340
    height: column.implicitHeight + 2 * Theme.spacing
    radius: Theme.radius
    color: Theme.surface
    border.color: Theme.border

    component DelayRow: Column {
        id: row
        property string label
        property real value
        signal adjusted(real value)
        width: parent.width
        spacing: Theme.spacingTiny

        Text {
            text: row.label + ": " + (row.value > 0 ? "+" : "") + row.value.toFixed(1) + " " + qsTr("s")
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
        }
        Row {
            spacing: Theme.spacingSmall
            ActionButton { text: "−0.1"; onClicked: row.adjusted(row.value - 0.1) }
            ActionButton { text: "+0.1"; onClicked: row.adjusted(row.value + 0.1) }
            ActionButton { text: qsTr("Reset"); enabled: row.value !== 0; onClicked: row.adjusted(0) }
        }
    }

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacing
        spacing: Theme.spacing

        Heading {
            text: qsTr("Playback")
            font.pixelSize: Theme.fontSizeLarge
        }
        Column {
            width: parent.width
            spacing: Theme.spacingTiny
            visible: !PlayerSession.inGroup

            Text {
                text: qsTr("Speed")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
            }
            Flow {
                width: parent.width
                spacing: Theme.spacingSmall
                Repeater {
                    model: [0.5, 0.75, 1, 1.25, 1.5, 2]
                    ActionButton {
                        required property real modelData
                        objectName: "speed" + modelData
                        text: modelData + "×"
                        primary: Math.abs(PlayerSession.baseRate - modelData) < 0.001
                        onClicked: PlayerSession.setRate(modelData)
                    }
                }
            }
        }
        Column {
            width: parent.width
            spacing: Theme.spacingTiny

            Text {
                text: PlayerSession.playMethod === "Transcode"
                    ? qsTr("Quality (the server converts this video)")
                    : qsTr("Quality")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
            }
            Flow {
                width: parent.width
                spacing: Theme.spacingSmall
                Repeater {
                    // Bits per second; 0 is the original.
                    model: [0, 20000000, 10000000, 6000000, 3000000, 1500000]
                    ActionButton {
                        required property real modelData
                        objectName: "quality" + modelData
                        text: modelData === 0 ? qsTr("Original") : qsTr("%1 Mbit/s").arg(modelData / 1000000)
                        primary: PlayerSession.maxBitrate === modelData
                        onClicked: PlayerSession.maxBitrate = modelData
                    }
                }
            }
        }
        DelayRow {
            label: qsTr("Subtitle delay")
            value: PlayerSession.subtitleDelay
            onAdjusted: (value) => PlayerSession.subtitleDelay = value
        }
        DelayRow {
            label: qsTr("Audio delay")
            value: PlayerSession.audioDelay
            onAdjusted: (value) => PlayerSession.audioDelay = value
        }
        Row {
            spacing: Theme.spacing
            ToggleSwitch {
                id: autoSkip
                checked: PlayerSession.autoSkip
                onToggled: PlayerSession.autoSkip = checked
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Skip intros and credits automatically")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }
}
