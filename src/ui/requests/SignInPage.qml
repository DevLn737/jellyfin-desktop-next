pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Signs in to Seerr as the user's Jellyfin account. The password goes to
// the Seerr server named on the page, not to the Jellyfin server, so the
// page says so and asks to confirm before sending; it is never kept.
RoutePage {
    id: page

    property string errorText
    property bool working: false

    title: qsTr("Sign in to Seerr")

    function submit() {
        if (user.text.length === 0 || password.text.length === 0 || working)
            return
        confirm.open()
    }

    Connections {
        target: Requests
        function onSignInFinished(outcome) {
            page.working = false
            password.text = ""
            if (outcome === 0) {
                page.errorText = ""
                Navigator.back()
            } else {
                page.errorText = Requests.outcomeText(outcome)
            }
        }
    }

    ColumnLayout {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingHuge
        width: Math.min(460, parent.width - 2 * Theme.pageMargin)
        spacing: Theme.spacing

        Heading { text: qsTr("Sign in to Seerr"); Layout.fillWidth: true }

        Text {
            objectName: "seerrHost"
            Layout.fillWidth: true
            text: Requests.serverHost
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge
            elide: Text.ElideMiddle
            Accessible.name: qsTr("Seerr server: %1").arg(Requests.serverHost)
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Use your Jellyfin user name and password. They are sent to this Seerr server, not to your Jellyfin server, and are not saved.")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }
        Text {
            objectName: "seerrCleartextWarning"
            Layout.fillWidth: true
            visible: Requests.cleartextOverInternet
            text: qsTr("This address uses plain http outside your local network: your password can be read on the way. Use an https address.")
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        InputField {
            id: user
            objectName: "seerrUser"
            Layout.fillWidth: true
            label: qsTr("User name")
            text: Library.signIn ? Library.signIn.userName : ""
            focus: text.length === 0
            inputMethodHints: Qt.ImhNoAutoUppercase
            onAccepted: password.forceActiveFocus()
        }
        InputField {
            id: password
            objectName: "seerrPassword"
            Layout.fillWidth: true
            label: qsTr("Password")
            password: true
            focus: user.text.length > 0
            onAccepted: page.submit()
        }

        Text {
            objectName: "seerrSignInError"
            Layout.fillWidth: true
            visible: page.errorText.length > 0
            text: page.errorText
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
        }

        RowLayout {
            spacing: Theme.spacing
            ActionButton {
                objectName: "seerrCancel"
                text: qsTr("Cancel")
                onClicked: Navigator.back()
            }
            ActionButton {
                objectName: "seerrSignInButton"
                text: page.working ? qsTr("Signing in…") : qsTr("Sign in")
                primary: true
                enabled: !page.working && user.text.length > 0 && password.text.length > 0
                onClicked: page.submit()
            }
        }
    }

    AppDialog {
        id: confirm
        objectName: "seerrConfirm"
        title: qsTr("Send your password to this server?")
        text: qsTr("Your Jellyfin password will be sent to %1 to sign in to Seerr.").arg(Requests.serverHost)
        acceptText: qsTr("Sign in")
        onAccepted: {
            page.errorText = ""
            page.working = true
            Requests.signIn(user.text, password.text)
        }
    }
}
