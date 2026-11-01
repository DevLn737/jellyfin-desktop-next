import QtQuick
import Jfd.Ui.Theme

// One shared look for "nothing here", "something went wrong" and "no
// connection". `kind` is "empty", "error" or "offline"; each has a default
// title; `description` adds a line. With `retryVisible` a button emits
// retry(). Screens put it where their content would be.
Item {
    id: state

    property string kind: "empty"
    property string title: kind === "error" ? qsTr("Something went wrong")
                         : kind === "offline" ? qsTr("No connection")
                         : qsTr("Nothing here yet")
    property string description
    // The same as description (a screen can read the message back).
    property alias text: state.description
    property bool retryVisible: kind !== "empty"

    signal retry()

    implicitWidth: 360
    implicitHeight: column.implicitHeight
    Accessible.role: Accessible.StaticText
    Accessible.name: description.length > 0 ? title + ". " + description : title

    Column {
        id: column
        width: Math.min(parent.width, 360)
        anchors.centerIn: parent
        spacing: Theme.spacing

        Icon {
            anchors.horizontalCenter: parent.horizontalCenter
            source: state.kind === "empty" ? Icons.library : Icons.error
            size: 40
            // The icon files are light; a red here would pick the dark twin.
            color: Theme.textSecondary
        }
        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: state.title
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge
            font.weight: Theme.fontWeightBold
            wrapMode: Text.Wrap
        }
        Text {
            width: parent.width
            visible: text.length > 0
            horizontalAlignment: Text.AlignHCenter
            text: state.description
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }
        ActionButton {
            objectName: "stateRetry"
            anchors.horizontalCenter: parent.horizontalCenter
            visible: state.retryVisible
            primary: true
            text: qsTr("Try again")
            onClicked: state.retry()
        }
    }
}
