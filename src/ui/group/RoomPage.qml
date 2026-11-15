import QtQuick
import QtQuick.Controls.Basic as Basic
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Group

// Inside a room (GroupPage shows it while GroupSession.inGroup): its
// members, the speed for everyone, the queue, how well we keep up, and
// leaving.
FocusScope {
    id: root

    readonly property var group: GroupSession
    readonly property var rates: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]

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

    Basic.ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
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

            Row {
                visible: root.group.rateSupported
                spacing: Theme.spacing

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Speed for everyone")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                }
                ChoiceBox {
                    id: rateBox
                    objectName: "rateBox"
                    width: Theme.tvMode ? 180 : 120
                    model: root.rates.map(r => ({ value: r, label: Number(r).toLocaleString(Qt.locale(), "g", 3) + "×" }))
                    currentIndex: Math.max(0, root.rates.indexOf(root.group.rate))
                    onActivated: root.group.setRate(root.rates[currentIndex])
                }
            }

            SectionHeader {
                text: qsTr("Members (%1)").arg(root.group.participants.length)
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
                    text: qsTr("Nothing queued. Start a film or episode with “Watch together” and everyone in the room gets it.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
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
