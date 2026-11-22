import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Group

// The group pill for the player's OSD, kept small: the first three
// viewers' initials, overlapping, with a dot on the last (green in sync,
// amber while catching up, red when the connection to the group is lost),
// and the number of viewers. The room's name is in the room's page and in
// the tooltip-free accessible name. How far we are from the group and the
// delay to the server are in the player's statistics (right-click). Hidden
// outside a group.
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
    Accessible.name: (osd.group.groupName.length > 0 ? qsTr("Room «%1»").arg(osd.group.groupName) : qsTr("Together"))
                     + " · " + label.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        // The first three viewers' initials, overlapping.
        Item {
            id: avatars
            anchors.verticalCenter: parent.verticalCenter
            readonly property int size: 24
            readonly property int step: 16
            readonly property int shown: Math.max(1, Math.min(3, osd.viewers))
            width: size + (shown - 1) * step
            height: size
            Repeater {
                model: osd.group.participants.slice(0, 3)
                delegate: Rectangle {
                    required property string modelData
                    required property int index
                    x: index * avatars.step
                    // The first on top, the others tucked under it.
                    z: 3 - index
                    width: avatars.size
                    height: avatars.size
                    radius: avatars.size / 2
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
            // The state of the sync, on the stack's corner.
            Rectangle {
                objectName: "groupSyncDot"
                z: 4
                x: avatars.width - width + 1
                y: avatars.height - height + 1
                width: 10
                height: 10
                radius: 5
                color: osd.dotColor
                border.width: 2
                border.color: Overlay.surface
                Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
            }
        }
        Text {
            id: label
            objectName: "memberCount"
            anchors.verticalCenter: parent.verticalCenter
            text: osd.group.connected ? String(osd.viewers) : osd.viewers + " · " + qsTr("reconnecting")
            color: Overlay.text
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Theme.fontWeightBold
        }
    }
}
