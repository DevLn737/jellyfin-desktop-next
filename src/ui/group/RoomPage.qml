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
            width: parent.width - 2 * Theme.pageMargin
            topPadding: Theme.spacingLarge
            bottomPadding: Theme.spacingHuge
            spacing: Theme.spacingLarge

            Column {
                width: parent.width
                spacing: Theme.spacingTiny
                Heading {
                    width: parent.width
                    text: qsTr("Watch together")
                }
                Text {
                    width: parent.width
                    text: qsTr("The video plays in step for everyone in the room.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
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

            // The room we are in: what it does, who is in it, and the way
            // to the player or out.
            Rectangle {
                objectName: "roomPanel"
                width: parent.width
                height: panel.implicitHeight + 2 * Theme.spacingLarge
                radius: Theme.radiusLarge + 4
                color: Theme.surface
                border.width: 1
                border.color: Theme.glassBorder

                Column {
                    id: panel
                    x: Theme.spacingLarge + 4
                    y: Theme.spacingLarge
                    width: parent.width - 2 * x
                    spacing: Theme.spacing

                    Row {
                        spacing: Theme.spacingSmall
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 10
                            height: 10
                            radius: 5
                            color: root.group.connected ? Theme.success : Theme.error
                        }
                        Text {
                            objectName: "groupState"
                            anchors.verticalCenter: parent.verticalCenter
                            text: (qsTr("You are in the room") + " · " + root.stateText(root.group.state)).toUpperCase()
                            color: root.group.connected ? Theme.success : Theme.error
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: Font.ExtraBold
                            font.letterSpacing: 0.8
                        }
                    }

                    Text {
                        objectName: "roomName"
                        width: parent.width
                        text: qsTr("Room «%1»").arg(root.group.groupName)
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeLarge + 10
                        font.weight: Font.ExtraBold
                        elide: Text.ElideRight
                    }

                    // What plays, the speed and how well we keep up.
                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: {
                            const parts = []
                            for (const entry of root.group.queue)
                                if (entry.current && entry.name.length > 0) parts.push(entry.name)
                            const sync = root.syncText()
                            if (sync.length > 0) parts.push(sync)
                            return parts.join(" · ")
                        }
                        color: root.group.connected ? Theme.textSecondary : Theme.error
                        font.pixelSize: Theme.fontSizeBody + 2
                        elide: Text.ElideRight
                    }
                    // The speed is changed in the player, by anyone in the
                    // room; here it is only shown.
                    Text {
                        objectName: "rateText"
                        visible: root.group.rateSupported
                        text: qsTr("Speed for everyone: %1").arg(Number(root.group.rate).toLocaleString(Qt.locale(), "g", 3) + "×")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                    }

                    // The members: initials and names; the delay only for
                    // ourselves, as the server shares no other member's.
                    Flow {
                        width: parent.width
                        spacing: Theme.spacingSmall
                        Repeater {
                            objectName: "memberList"
                            model: root.group.participants
                            Rectangle {
                                id: chip
                                required property string modelData
                                required property int index
                                width: chipRow.implicitWidth + 2 * 6 + 8
                                height: 36
                                radius: 18
                                color: Qt.alpha(Theme.text, 0.06)
                                Accessible.role: Accessible.StaticText
                                Accessible.name: modelData
                                Row {
                                    id: chipRow
                                    x: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: Theme.spacingSmall
                                    Rectangle {
                                        width: 28
                                        height: 28
                                        radius: 14
                                        color: Qt.hsla(chip.modelData.split("").reduce((h, c) => (h * 31 + c.charCodeAt(0)) % 360, 7) / 360, 0.45, 0.45, 1)
                                        Text {
                                            anchors.centerIn: parent
                                            text: chip.modelData.charAt(0).toUpperCase()
                                            color: "white"
                                            font.pixelSize: Theme.fontSizeSmall
                                            font.weight: Font.ExtraBold
                                        }
                                    }
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: chip.modelData
                                        color: Theme.text
                                        font.pixelSize: Theme.fontSizeBody
                                        font.weight: Font.Bold
                                    }
                                }
                            }
                        }
                    }
                    Text {
                        objectName: "pingText"
                        width: parent.width
                        visible: root.group.pingMs >= 0
                        text: qsTr("Your delay to the server: %1 ms").arg(root.group.pingMs)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                    }

                    Flow {
                        width: parent.width
                        spacing: Theme.spacingSmall
                        topPadding: Theme.spacingSmall
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
                            objectName: "nextButton"
                            text: qsTr("Next")
                            visible: queue.count > 1
                            onClicked: root.group.next()
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

            SectionHeader {
                text: qsTr("Queue")
            }
            Column {
                width: Math.min(parent.width, 720)
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
        }
    }
}
