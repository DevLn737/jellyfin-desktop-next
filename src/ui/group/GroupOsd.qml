import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Group

// The group badge for the player's OSD: a dot (green in sync, amber while
// catching up, red when the connection to the group is lost) and the
// number of viewers. How far we are from the group and the delay to the
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

    visible: group.inGroup
    implicitWidth: row.implicitWidth + 2 * Theme.spacing
    implicitHeight: Theme.controlHeight
    radius: Theme.radius
    color: Qt.alpha(Overlay.surface, 0.85)
    border.width: 1
    border.color: Overlay.border
    Accessible.role: Accessible.StaticText
    Accessible.name: label.text

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacingSmall

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 8
            height: 8
            radius: 4
            color: osd.dotColor
            Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
        }
        Icon {
            anchors.verticalCenter: parent.verticalCenter
            source: Icons.group
            size: 16
        }
        Text {
            id: label
            objectName: "memberCount"
            anchors.verticalCenter: parent.verticalCenter
            text: {
                const line = qsTr("Together: %n viewer(s)", "", osd.viewers)
                return osd.group.connected ? line : line + ", " + qsTr("reconnecting")
            }
            color: Overlay.text
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
