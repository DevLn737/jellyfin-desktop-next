import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A room of watching together (a SyncPlay group) in the rooms list: its
// name, what it is doing and who is in it. `roomState` is "idle",
// "waiting", "paused" or "playing"; `participants` the members' names (one
// per session, as the server lists them). `selected` marks the room whose
// card is open. Enter, a click or Select opens it (clicked()).
//
// Stock Jellyfin 12.2 tells nothing more about a room; marks for a limit,
// a password or an invitation come with the rooms extension of the server
// fork (package 3.9, steps 3 and 4), not before.
T.ItemDelegate {
    id: card
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(card)

    property string roomState: "idle"
    property var participants: []
    property bool selected: false

    readonly property string stateText: roomState === "playing" ? qsTr("Playing")
                                      : roomState === "paused" ? qsTr("Paused")
                                      : roomState === "waiting" ? qsTr("Waiting for everyone")
                                      : qsTr("Nothing playing")
    readonly property int memberCount: participants ? participants.length : 0
    readonly property string memberText: memberCount > 0 ? participants.join(", ") : qsTr("Nobody here")

    implicitWidth: Theme.tvMode ? 400 : 280
    implicitHeight: content.implicitHeight + topPadding + bottomPadding
    padding: Theme.spacing
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.role: Accessible.Button
    Accessible.name: text + ", " + stateText + ", " + memberText

    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()

    contentItem: Column {
        id: content
        spacing: Theme.spacingTiny

        Row {
            width: parent.width
            spacing: Theme.spacingSmall

            Text {
                objectName: "roomCardTitle"
                width: parent.width - count.width - parent.spacing
                text: card.text
                color: card.colors.text
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Theme.fontWeightBold
                elide: Text.ElideRight
            }
            // The number of members, with the members' icon.
            Row {
                id: count
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingTiny
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    source: Icons.group
                    size: Math.round(Theme.iconSize * 0.8)
                    color: card.colors.textSecondary
                }
                Text {
                    objectName: "roomCardCount"
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.memberCount.toString()
                    color: card.colors.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
            }
        }
        Text {
            objectName: "roomCardState"
            width: parent.width
            text: card.stateText
            // Playing in the accent's place would fail contrast on the
            // raised surface in the light theme: the plain text colour.
            color: card.roomState === "playing" ? card.colors.text : card.colors.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.weight: card.roomState === "playing" ? Theme.fontWeightBold : Theme.fontWeightNormal
            elide: Text.ElideRight
        }
        Text {
            objectName: "roomCardMembers"
            width: parent.width
            text: card.memberText
            color: card.colors.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
        }
    }

    background: Rectangle {
        objectName: "roomCardBackground"
        radius: Theme.radius
        // The open room keeps the raised fill: its line needs 3:1 on it.
        color: card.down || (card.hovered && !card.selected) ? card.colors.surfaceHover : card.colors.surfaceRaised
        // The open room: a thin line in the focus ring's colour (3:1 on
        // every surface), inside the card where the ring goes outside.
        border.width: card.selected ? Math.max(1, Theme.focusRingWidth / 2) : 0
        border.color: card.colors.focusRing
        FocusFrame { shown: card.visualFocus }
    }
}
