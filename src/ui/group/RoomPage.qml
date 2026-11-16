import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Group

// Inside a room (GroupPage shows it while GroupSession.inGroup): its
// members, the speed for everyone (changed in the player), the queue, how well we keep up, and
// leaving.
FocusScope {
    id: root

    readonly property var group: GroupSession

    function stateText(state) {
        switch (state) {
        case "playing": return qsTr("Playing")
        case "paused": return qsTr("Paused")
        case "waiting": return qsTr("Waiting for everyone")
        default: return qsTr("Nothing playing")
        }
    }

    function syncText() {
        if (!group.connected)
            return qsTr("Connection lost, rejoining…")
        if (!group.hasDrift)
            return ""
        const ms = Math.abs(group.driftMs)
        if (group.correction === "skip")
            return qsTr("Catching up: jumping %1 ms").arg(ms)
        if (group.correction === "speed")
            return qsTr("Catching up: %1 ms").arg(ms)
        return qsTr("In sync (%1 ms)").arg(ms)
    }

    // A Flickable, not a ScrollView: ScrollView takes the arrow keys to
    // scroll, so they would never reach the shell's SpatialNavigation.
    Flickable {
        SmoothWheel {}
        anchors.fill: parent
        contentHeight: content.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: content
            x: Theme.pageMargin
            width: Math.min(parent.width - 2 * Theme.pageMargin, 720)
            topPadding: Theme.spacingHuge
            bottomPadding: Theme.spacingHuge
            spacing: Theme.spacingLarge

            Heading {
                objectName: "roomName"
                width: parent.width
                text: root.group.groupName
            }

            Text {
                objectName: "errorText"
                width: parent.width
                visible: text.length > 0
                text: root.group.errorText
                color: Theme.error
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }

            Row {
                spacing: Theme.spacing

                GroupOsd {
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    objectName: "groupState"
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.stateText(root.group.state)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.syncText()
                    color: root.group.connected ? Theme.textSecondary : Theme.error
                    font.pixelSize: Theme.fontSizeBody
                }
            }

            // The speed is changed in the player, by anyone in the room;
            // here it is only shown.
            Text {
                objectName: "rateText"
                visible: root.group.rateSupported
                text: qsTr("Speed for everyone: %1").arg(Number(root.group.rate).toLocaleString(Qt.locale(), "g", 3) + "×")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }

            SectionHeader {
                text: qsTr("Members (%1)").arg(root.group.participants.length)
            }
            // Only our own: the server shares no other member's.
            Text {
                objectName: "pingText"
                width: parent.width
                visible: root.group.pingMs >= 0
                text: qsTr("Your delay to the server: %1 ms").arg(root.group.pingMs)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            Column {
                width: parent.width
                Repeater {
                    objectName: "memberList"
                    model: root.group.participants
                    ListItem {
                        required property string modelData
                        width: parent.width
                        text: modelData
                        enabled: false
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacing
                SectionHeader {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Queue")
                }
                ActionButton {
                    objectName: "nextButton"
                    text: qsTr("Next")
                    visible: queue.count > 1
                    onClicked: root.group.next()
                }
            }
            Column {
                width: parent.width
                Repeater {
                    id: queue
                    objectName: "queueList"
                    model: root.group.queue
                    ListItem {
                        required property var modelData
                        width: parent.width
                        iconSource: modelData.current ? Icons.play : ""
                        text: modelData.name.length > 0 ? modelData.name : qsTr("Loading…")
                        highlighted: modelData.current
                        enabled: false
                    }
                }
                Text {
                    visible: queue.count === 0
                    width: parent.width
                    text: qsTr("Nothing queued. Press Play on any film or episode and it starts for everyone in the room.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
            }

            // Back to the room's film after looking around the library.
            ActionButton {
                objectName: "openPlayerButton"
                visible: queue.count > 0 && root.group.state !== "idle" && root.group.state !== ""
                primary: true
                iconSource: Icons.play
                text: qsTr("Open the player")
                onClicked: Navigator.navigate("player")
            }

            ActionButton {
                id: leaveButton
                objectName: "leaveButton"
                text: qsTr("Leave room")
                focus: true
                enabled: !root.group.busy
                onClicked: root.group.leaveGroup()
            }
        }
    }
}
