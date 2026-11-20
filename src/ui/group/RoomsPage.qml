import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Group

// The rooms on the server, outside a room (GroupPage shows it while not in
// one): the live list of RoomCards, a room's card with its members and
// Join, and Create room for users who may create. The list is asked for
// while this page shows in the active window (RoomsSession.active), at
// once and then every 5 s.
//
// Keys: arrows, Tab and Enter as everywhere (TV mode: arrows, Select and
// Back only; the room's name from the on-screen keyboard); Back (Esc)
// closes an open card before it leaves the page.
FocusScope {
    id: root

    readonly property var rooms: RoomsSession
    readonly property var group: GroupSession
    readonly property bool cardOpen: rooms.selectedId.length > 0
    readonly property bool online: rooms.online
    readonly property bool inGroup: group.inGroup

    function stateText(state) {
        switch (state) {
        case "playing": return qsTr("Playing")
        case "paused": return qsTr("Paused")
        case "waiting": return qsTr("Waiting for everyone")
        default: return qsTr("Nothing playing")
        }
    }

    function openCard(roomId) {
        root.rooms.select(roomId)
        if (joinButton.enabled)
            joinButton.forceActiveFocus(Qt.TabFocusReason)
        else
            backButton.forceActiveFocus(Qt.TabFocusReason)
    }

    // Back to the list, onto the card that was open; false without one.
    function closeCard() {
        if (!root.cardOpen)
            return false
        const index = root.rooms.indexOf(root.rooms.selectedId)
        root.rooms.select("")
        const card = index >= 0 ? roomList.itemAt(index) : null
        if (card)
            card.forceActiveFocus(Qt.TabFocusReason)
        else
            root.focusFirst()
        return true
    }

    function focusFirst() {
        if (createButton.visible)
            nameField.forceActiveFocus(Qt.TabFocusReason)
        else if (roomList.count > 0)
            roomList.itemAt(0).forceActiveFocus(Qt.TabFocusReason)
        else
            refreshButton.forceActiveFocus(Qt.TabFocusReason)
    }

    Keys.onEscapePressed: (event) => { event.accepted = root.closeCard() }
    // Left to the page's own nearest control first (the room to the left,
    // Join from Back to rooms): the page stack's KeyNavigation.left would
    // otherwise take every Left to the sidebar. Nothing to the left: on to
    // the sidebar as on every page.
    Keys.onLeftPressed: (event) => { event.accepted = root.SpatialNavigation.move(SpatialNavigation.Left) }
    Keys.onBackPressed: (event) => { event.accepted = root.closeCard() }

    // Joined: the card that led there is done with. Leaving the room shows
    // the list, not that room's card again.
    onInGroupChanged: {
        if (root.inGroup)
            root.rooms.select("")
    }

    // Poll only while the page shows and the window is the active one.
    Binding {
        target: root.rooms
        property: "active"
        value: root.visible && root.Window.active
    }
    Component.onDestruction: root.rooms.active = false

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
                    text: root.cardOpen && !root.rooms.selectedGone ? root.rooms.selected.name || "" : qsTr("Watch together")
                }
                Text {
                    width: parent.width
                    visible: !root.cardOpen
                    text: qsTr("Rooms are seen by everyone on this server. The video plays in step for everyone in the room.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
            }

            Text {
                objectName: "errorText"
                width: Math.min(parent.width, 720)
                visible: text.length > 0
                text: root.group.errorText
                color: Theme.error
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }

            StateMessage {
                objectName: "roomsOffline"
                width: Math.min(parent.width, 720)
                visible: !root.online
                kind: "offline"
                description: qsTr("Rooms show up again when the server can be reached.")
                retryVisible: false
            }

            StateMessage {
                objectName: "roomsUnavailable"
                width: Math.min(parent.width, 720)
                visible: root.online && !root.rooms.available
                kind: "empty"
                title: qsTr("Watching together is turned off for your account")
                retryVisible: false
            }

            // ---- The list ------------------------------------------------

            Row {
                visible: root.online && root.rooms.canCreate && !root.cardOpen
                width: Math.min(parent.width, 720)
                spacing: Theme.spacing

                InputField {
                    id: nameField
                    objectName: "roomNameField"
                    width: parent.width - createButton.width - parent.spacing
                    label: qsTr("New room")
                    placeholderText: qsTr("Watching together")
                    focus: true
                    onAccepted: createButton.clicked()
                }
                ActionButton {
                    id: createButton
                    objectName: "createButton"
                    anchors.bottom: parent.bottom
                    primary: true
                    text: qsTr("Create room")
                    enabled: !root.group.busy
                    onClicked: root.rooms.create(nameField.text)
                }
            }

            Row {
                visible: root.online && root.rooms.available && !root.cardOpen
                spacing: Theme.spacing

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Rooms on this server")
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge + 2
                    font.weight: Font.ExtraBold
                }
                LoadingIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    running: root.rooms.loading && !root.rooms.loaded
                    visible: running
                }
                ActionButton {
                    id: refreshButton
                    objectName: "refreshButton"
                    text: qsTr("Refresh")
                    // Not disabled while a poll is in flight (refresh() then
                    // does nothing): a disabled button would drop the focus
                    // and leave the Tab chain every few seconds.
                    onClicked: root.rooms.refresh()
                }
            }

            Flow {
                id: list
                objectName: "roomList"
                visible: root.online && root.rooms.available && !root.cardOpen
                width: parent.width
                spacing: Theme.spacing

                Repeater {
                    id: roomList
                    model: root.rooms
                    // A changed room updates its card in place; one that did
                    // not change keeps it as it is (RoomsModel compares).
                    RoomCard {
                        required property var model
                        objectName: "room_" + model.roomId
                        text: model.name
                        roomState: model.roomState
                        participants: model.participants
                        selected: model.roomId === root.rooms.selectedId
                        enabled: !root.group.busy
                        onClicked: root.openCard(model.roomId)
                    }
                }
            }

            StateMessage {
                objectName: "roomsEmpty"
                width: Math.min(parent.width, 720)
                visible: root.online && root.rooms.available && !root.cardOpen && root.rooms.loaded
                         && root.rooms.count === 0 && !root.rooms.failed
                kind: "empty"
                title: qsTr("No rooms right now")
                // Not "the server has none": rooms playing what this user
                // may not see are not listed to them.
                description: root.rooms.canCreate
                             ? qsTr("Rooms you can join show up here. Create one above, then press Play on any film or episode.")
                             : qsTr("Rooms you can join show up here when someone creates one.")
            }

            StateMessage {
                objectName: "roomsFailed"
                width: Math.min(parent.width, 720)
                visible: root.online && root.rooms.available && !root.cardOpen && root.rooms.failed
                         && root.rooms.count === 0
                kind: "error"
                description: qsTr("Could not load the rooms.")
                onRetry: root.rooms.refresh()
            }

            // ---- A room's card -------------------------------------------

            Column {
                id: card
                objectName: "roomDetails"
                visible: root.cardOpen
                width: Math.min(parent.width, 720)
                spacing: Theme.spacingLarge

                // The open room closed under us: a line says so, Join is off.
                Text {
                    objectName: "roomDetailsState"
                    width: parent.width
                    text: root.rooms.selectedGone ? qsTr("This room has closed.")
                                                  : root.stateText(root.rooms.selected.state || "")
                    color: root.rooms.selectedGone ? Theme.error : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }

                SectionHeader {
                    visible: !root.rooms.selectedGone
                    text: qsTr("Members (%1)").arg(root.rooms.selected.participantCount || 0)
                }
                Column {
                    visible: !root.rooms.selectedGone
                    width: parent.width
                    Repeater {
                        objectName: "roomMembers"
                        model: root.rooms.selected.participants || []
                        ListItem {
                            required property string modelData
                            width: parent.width
                            iconSource: Icons.group
                            text: modelData
                            enabled: false
                        }
                    }
                }

                Row {
                    spacing: Theme.spacing
                    ActionButton {
                        id: joinButton
                        objectName: "joinButton"
                        primary: true
                        text: qsTr("Join")
                        enabled: root.rooms.canJoin && !root.group.busy && !root.rooms.selectedGone && root.online
                        onClicked: root.rooms.join(root.rooms.selectedId)
                    }
                    ActionButton {
                        id: backButton
                        objectName: "backToRooms"
                        text: qsTr("Back to rooms")
                        onClicked: root.closeCard()
                    }
                }
            }
        }
    }
}
