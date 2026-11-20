import QtQuick
import Jfd.Ui.Theme

// A short message in the bottom right corner that hides itself. One per
// window (the shell has it); screens call show() through the shell.
// kind: "info" or "error". With linkText and link it has a button that opens
// the link in the browser, and stays longer.
Rectangle {
    id: toast
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(toast)

    property string message
    property string kind: "info"
    property string linkText
    property url link
    readonly property bool hasLink: linkText.length > 0 && link.toString().length > 0
    property int timeout: hasLink ? 15000 : 5000

    function show(text, messageKind, messageLinkText, messageLink) {
        message = text
        kind = messageKind === undefined ? "info" : messageKind
        linkText = messageLinkText === undefined ? "" : messageLinkText
        link = messageLink === undefined ? "" : messageLink
        state = "shown"
        timer.restart()
    }
    function hide() { state = "" }

    // A card in the bottom right corner, over the page and clear of the
    // content's centre. Sized from its parts, so the text wraps only past
    // maxWidth and never runs under the button.
    readonly property real maxWidth: Math.max(240, Math.min(560, (parent ? parent.width : 560) - 2 * Theme.spacingLarge))
    readonly property real sideWidth: (kind === "error" ? 18 + row.spacing : 0)
                                      + (hasLink ? linkButton.implicitWidth + row.spacing : 0)
    anchors.right: parent.right
    anchors.rightMargin: Theme.spacingLarge
    anchors.bottom: parent.bottom
    anchors.bottomMargin: Theme.spacingLarge
    width: row.implicitWidth + 2 * Theme.spacingLarge
    height: row.implicitHeight + 2 * Theme.spacing
    radius: Theme.radiusLarge
    color: colors.surfaceRaised
    border.width: 1
    border.color: kind === "error" ? colors.error : hasLink ? colors.accent : colors.border
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

    // A soft shadow so the card stands apart from what is under it.
    Rectangle {
        z: -1
        anchors.fill: parent
        anchors.margins: -1
        anchors.topMargin: 3
        anchors.bottomMargin: -5
        radius: parent.radius + 2
        color: "#66000000"
    }

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacing

        Icon {
            visible: toast.kind === "error"
            source: Icons.error
            size: 18
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            objectName: "toastText"
            width: Math.min(implicitWidth, toast.maxWidth - 2 * Theme.spacingLarge - toast.sideWidth)
            text: toast.message
            color: colors.text
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            anchors.verticalCenter: parent.verticalCenter
        }
        ActionButton {
            id: linkButton
            objectName: "toastLink"
            visible: toast.hasLink
            primary: true
            text: toast.linkText
            anchors.verticalCenter: parent.verticalCenter
            onClicked: {
                Qt.openUrlExternally(toast.link)
                toast.hide()
            }
        }
    }

    TapHandler { onTapped: toast.hide() }
}
