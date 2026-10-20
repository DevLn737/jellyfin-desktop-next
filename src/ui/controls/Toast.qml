import QtQuick
import Jfd.Ui.Theme

// A short message at the bottom of the window that hides itself. One per
// window (the shell has it); screens call show() through the shell.
// kind: "info" or "error".
Rectangle {
    id: toast

    property string message
    property string kind: "info"
    property int timeout: 5000

    function show(text, messageKind) {
        message = text
        kind = messageKind === undefined ? "info" : messageKind
        state = "shown"
        timer.restart()
    }
    function hide() { state = "" }

    anchors.horizontalCenter: parent.horizontalCenter
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacingHuge
    width: Math.min(row.implicitWidth + 2 * Theme.spacingLarge, parent.width - 2 * Theme.spacingHuge)
    height: row.implicitHeight + 2 * Theme.spacing
    radius: Theme.radius
    color: Theme.surfaceRaised
    border.width: 1
    border.color: kind === "error" ? Theme.error : Theme.border
    opacity: 0
    visible: opacity > 0
    z: 1000
    Accessible.role: Accessible.AlertMessage
    Accessible.name: message

    states: State {
        name: "shown"
        PropertyChanges { toast.opacity: 1 }
    }
    transitions: Transition {
        NumberAnimation { property: "opacity"; duration: Theme.durationNormal }
    }

    Timer {
        id: timer
        interval: toast.timeout
        onTriggered: toast.hide()
    }

    Row {
        id: row
        anchors.centerIn: parent
        width: Math.min(implicitWidth, toast.width - 2 * Theme.spacingLarge)
        spacing: Theme.spacingSmall

        Icon {
            visible: toast.kind === "error"
            source: Icons.error
            size: 18
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            width: Math.min(implicitWidth, row.width - (toast.kind === "error" ? 18 + row.spacing : 0))
            text: toast.message
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    TapHandler { onTapped: toast.hide() }
}
