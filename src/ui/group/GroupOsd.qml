import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Group

// The group pill for the player's OSD: the first viewers' initials, a dot
// (green in sync, amber while catching up, red when the connection to the
// group is lost), the room's name and the number of viewers. How far we are from the group and the delay to the
// server are in the player's statistics (right-click). Hidden outside a
// group.
Rectangle {
    id: osd

    // On the video: the dark palette in every theme (see Theme.colorsFor).
    readonly property var colorScope: Overlay.colors

    readonly property var group: GroupSession
    readonly property color dotColor: !group.connected ? Overlay.error
                                      : group.correction.length > 0 ? "#d29922"
                                      : Overlay.success
    readonly property int viewers: group.participants.length
    // A colour per name, the same on every screen.
    readonly property var avatarColors: ["#c0566b", "#5b6fd6", "#3f9a7f", "#c08a3e", "#8a5bc0"]

    function avatarColor(name) {
        let sum = 0
        for (let i = 0; i < name.length; ++i) sum = (sum + name.charCodeAt(i)) % 997
        return avatarColors[sum % avatarColors.length]
    }

    visible: group.inGroup
    implicitWidth: row.implicitWidth + 2 * Theme.spacing
    implicitHeight: Theme.controlHeight
    radius: height / 2
    color: Qt.alpha(Overlay.surface, 0.85)
    border.width: 1
    border.color: Overlay.border
    Accessible.role: Accessible.StaticText
    Accessible.name: label.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        // The first three viewers' initials, overlapping.
        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: -6
            Repeater {
                model: osd.group.participants.slice(0, 3)
                delegate: Rectangle {
                    required property string modelData
                    width: 24
                    height: 24
                    radius: 12
                    color: osd.avatarColor(modelData)
                    border.width: 2
                    border.color: Overlay.surface
                    Text {
                        anchors.centerIn: parent
                        text: parent.modelData.length > 0 ? parent.modelData.charAt(0).toUpperCase() : "?"
                        color: "white"
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                }
            }
        }
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: osd.dotColor
            Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
        }
        Text {
            id: label
            objectName: "memberCount"
            anchors.verticalCenter: parent.verticalCenter
            text: {
                const name = osd.group.groupName.length > 0 ? qsTr("Room «%1»").arg(osd.group.groupName) : qsTr("Together")
                const line = name + " · " + osd.viewers
                return osd.group.connected ? line : line + ", " + qsTr("reconnecting")
            }
            color: Overlay.text
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Theme.fontWeightBold
        }
    }
}
