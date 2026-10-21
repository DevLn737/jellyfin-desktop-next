import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Group

// The group indicator for the player's OSD: a dot (green in sync, amber
// while catching up, red when the connection to the group is lost) and the
// number of members. Hidden outside a group.
Row {
    id: osd

    readonly property var group: GroupSession
    readonly property color dotColor: !group.connected ? Theme.error
                                      : group.correction.length > 0 ? "#d29922"
                                      : Theme.success

    visible: group.inGroup
    spacing: Theme.spacingSmall
    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("In a group of %1").arg(group.participants.length)

    Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 10
        height: 10
        radius: 5
        color: osd.dotColor
        Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
    }
    Icon {
        anchors.verticalCenter: parent.verticalCenter
        source: Icons.group
        size: 18
    }
    Text {
        objectName: "memberCount"
        anchors.verticalCenter: parent.verticalCenter
        text: osd.group.participants.length
        color: Theme.text
        font.pixelSize: Theme.fontSizeBody
    }
}
