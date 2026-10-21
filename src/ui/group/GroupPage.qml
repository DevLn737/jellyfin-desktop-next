import QtQuick
import QtQuick.Controls.Basic as Basic
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Group

// "Watch together": outside a group, the server's groups and creating one;
// in a group, its members, speed, queue and how well we keep up.
RoutePage {
    id: page

    title: qsTr("Watch together")

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

    Component.onCompleted: if (!group.inGroup) group.refreshGroups()

    // The application's AppShell (objectName "shell") shows toasts.
    function shell() {
        for (let item = page.parent; item; item = item.parent)
            if (item.objectName === "shell")
                return item
        return null
    }

    Connections {
        target: page.group
        function onNotice(text, error) {
            const shell = page.shell()
            if (shell)
                error ? shell.showError(text) : shell.showMessage(text)
        }
    }

    Basic.ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        Column {
            id: content
            x: Theme.spacingHuge
            width: Math.min(parent.width - 2 * Theme.spacingHuge, 720)
            topPadding: Theme.spacingHuge
            bottomPadding: Theme.spacingHuge
            spacing: Theme.spacingLarge

            Heading {
                width: parent.width
                text: page.group.inGroup ? page.group.groupName : page.title
            }

            Text {
                objectName: "errorText"
                width: parent.width
                visible: text.length > 0
                text: page.group.errorText
                color: Theme.error
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }

            // ---- Outside a group -------------------------------------------

            Row {
                visible: !page.group.inGroup
                width: parent.width
                spacing: Theme.spacing

                InputField {
                    id: nameField
                    objectName: "groupNameField"
                    width: parent.width - createButton.width - parent.spacing
                    label: qsTr("New group")
                    placeholderText: qsTr("Watching together")
                    focus: true
                    onAccepted: createButton.clicked()
                }
                ActionButton {
                    id: createButton
                    objectName: "createButton"
                    anchors.bottom: parent.bottom
                    primary: true
                    text: qsTr("Create")
                    enabled: !page.group.busy
                    onClicked: page.group.createGroup(nameField.text)
                }
            }

            Row {
                visible: !page.group.inGroup
                width: parent.width
                spacing: Theme.spacing

                SectionHeader {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Groups on this server")
                }
                LoadingIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    running: page.group.busy
                    visible: running
                }
                ActionButton {
                    objectName: "refreshButton"
                    text: qsTr("Refresh")
                    enabled: !page.group.busy
                    onClicked: page.group.refreshGroups()
                }
            }

            Column {
                visible: !page.group.inGroup
                width: parent.width

                Repeater {
                    id: groups
                    objectName: "groupList"
                    model: page.group.groups
                    ListItem {
                        required property var modelData
                        objectName: "group_" + modelData.id
                        width: parent.width
                        iconSource: Icons.group
                        text: modelData.name
                        subtitle: modelData.participants.join(", ")
                        trailingText: page.stateText(modelData.state)
                        enabled: !page.group.busy
                        onClicked: page.group.joinGroup(modelData.id)
                    }
                }
                Text {
                    visible: groups.count === 0 && !page.group.busy
                    text: qsTr("No groups yet. Create one, or start one from a film or episode with “Watch together”.")
                    width: parent.width
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
            }

            // ---- In a group ------------------------------------------------

            Row {
                visible: page.group.inGroup
                spacing: Theme.spacing

                GroupOsd {
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    objectName: "groupState"
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.stateText(page.group.state)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.syncText()
                    color: page.group.connected ? Theme.textSecondary : Theme.error
                    font.pixelSize: Theme.fontSizeBody
                }
            }

            Row {
                visible: page.group.inGroup && page.group.rateSupported
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
                    width: 120
                    model: page.rates.map(r => ({ value: r, label: Number(r).toLocaleString(Qt.locale(), "g", 3) + "×" }))
                    currentIndex: Math.max(0, page.rates.indexOf(page.group.rate))
                    onActivated: page.group.setRate(page.rates[currentIndex])
                }
            }

            SectionHeader {
                visible: page.group.inGroup
                text: qsTr("Members (%1)").arg(page.group.participants.length)
            }
            Column {
                visible: page.group.inGroup
                width: parent.width
                Repeater {
                    objectName: "memberList"
                    model: page.group.participants
                    ListItem {
                        required property string modelData
                        width: parent.width
                        text: modelData
                        enabled: false
                    }
                }
            }

            Row {
                visible: page.group.inGroup
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
                    onClicked: page.group.next()
                }
            }
            Column {
                visible: page.group.inGroup
                width: parent.width
                Repeater {
                    id: queue
                    objectName: "queueList"
                    model: page.group.queue
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
                    text: qsTr("Nothing queued. Start a film or episode with “Watch together” and everyone in the group gets it.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
            }

            ActionButton {
                objectName: "leaveButton"
                visible: page.group.inGroup
                text: qsTr("Leave group")
                enabled: !page.group.busy
                onClicked: page.group.leaveGroup()
            }
        }
    }
}
