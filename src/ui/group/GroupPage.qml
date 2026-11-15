import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Group

// The route "group", "Watch together": outside a room the rooms on the
// server (RoomsPage), in one the room itself (RoomPage).
RoutePage {
    id: page

    title: qsTr("Watch together")

    readonly property var group: GroupSession
    readonly property bool inGroup: group.inGroup

    // The application's AppShell (objectName "shell") shows toasts.
    function shell() {
        for (let item = page.parent; item; item = item.parent)
            if (item.objectName === "shell")
                return item
        return null
    }

    // Joined or left: the focus goes to the page now shown, once the one
    // hidden gave it back to this page (not when it is elsewhere, such as
    // the sidebar).
    onInGroupChanged: Qt.callLater(() => {
        if (page.activeFocus)
            (page.inGroup ? room : rooms).forceActiveFocus(Qt.TabFocusReason)
    })

    Connections {
        target: page.group
        function onNotice(text, error) {
            const shell = page.shell()
            if (shell)
                error ? shell.showError(text) : shell.showMessage(text)
        }
    }

    RoomsPage {
        id: rooms
        objectName: "roomsPage"
        anchors.fill: parent
        visible: !page.inGroup
        focus: !page.inGroup
    }

    RoomPage {
        id: room
        objectName: "roomPage"
        anchors.fill: parent
        visible: page.inGroup
        focus: page.inGroup
    }
}
