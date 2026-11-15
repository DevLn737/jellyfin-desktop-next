pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Sign-in: pick a server (found on the network, saved, or typed), then a
// password or Quick Connect. The application opens the home page once
// signed in. Back goes a step back (from the code to the password, from the
// password to the servers). In TV mode Quick Connect, which needs no
// typing, comes first and has the focus; the fields open the on-screen
// keyboard.
RoutePage {
    id: page

    readonly property SignIn signIn: Library.signIn
    readonly property int step: signIn ? signIn.step : SignIn.ChooseServer

    title: qsTr("Sign in")

    // TV mode: Quick Connect leads the password form.
    readonly property bool quickConnectFirst: Theme.tvMode && signIn !== null && signIn.quickConnectAvailable

    // Back: one step back; on the first step the shell's Back.
    function stepBack() {
        if (!signIn)
            return false
        if (step === SignIn.QuickConnect)
            signIn.cancelQuickConnect()
        else if (step === SignIn.Credentials)
            signIn.back()
        else
            return false
        return true
    }
    Keys.onEscapePressed: (event) => { event.accepted = page.stepBack() }
    Keys.onBackPressed: (event) => { event.accepted = page.stepBack() }

    Component.onCompleted: {
        if (signIn && step === SignIn.ChooseServer)
            signIn.discover()
    }

    Flickable {
        SmoothWheel {}
        anchors.fill: parent
        contentHeight: Math.max(height, column.implicitHeight + 2 * Theme.spacingHuge)
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column
            objectName: "loginColumn"
            width: Math.min(parent.width - 2 * Theme.spacingHuge, Theme.tvMode ? 760 : 420)
            x: (parent.width - width) / 2
            // The error line does not count: the form stays where it was
            // when a message appears above it. Never in a TV's outer 5 %.
            y: Math.max(Theme.spacingHuge, Math.round(page.height * Theme.safeMargin),
                        (page.height - implicitHeight + (loginError.visible ? loginError.height + spacing : 0)) / 3)
            spacing: Theme.spacingLarge

            Text {
                text: "Jellyfin"
                color: Theme.accent
                font.pixelSize: Theme.fontSizeHeadline
                font.weight: Theme.fontWeightBold
            }

            Heading {
                width: parent.width
                text: page.step === SignIn.ChooseServer ? qsTr("Connect to a server")
                    : page.step === SignIn.QuickConnect ? qsTr("Quick Connect")
                    : page.signIn ? page.signIn.serverName : ""
            }

            Text {
                id: loginError
                objectName: "loginError"
                width: parent.width
                visible: text.length > 0
                text: page.signIn ? page.signIn.errorText : ""
                color: Theme.error
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }

            Loader {
                width: parent.width
                sourceComponent: page.step === SignIn.ChooseServer ? chooseServer
                    : page.step === SignIn.Credentials ? credentials
                    : page.step === SignIn.QuickConnect ? quickConnect
                    : waiting
                focus: true
            }
        }
    }

    Component {
        id: waiting
        Item {
            implicitHeight: Theme.controlHeight * 2
            LoadingIndicator { anchors.centerIn: parent }
        }
    }

    Component {
        id: chooseServer
        FocusScope {
            id: chooser
            implicitHeight: serverColumn.implicitHeight
            // The Loader passes its focus on only to an item that asks.
            focus: true

            function connect() {
                if (address.text.trim().length > 0)
                    page.signIn.connectToServer(address.text.trim())
            }

            Column {
                id: serverColumn
                width: parent.width
                spacing: Theme.spacing

                SectionHeader {
                    visible: saved.count > 0
                    text: qsTr("Your servers")
                }
                Repeater {
                    id: saved
                    model: page.signIn ? page.signIn.savedServers : []
                    ListItem {
                        id: savedServer
                        required property var modelData
                        width: serverColumn.width
                        text: modelData.name
                        subtitle: modelData.userName ? modelData.userName + " · " + modelData.address : modelData.address
                        iconSource: Icons.library
                        onClicked: page.signIn.chooseSavedServer(modelData.id)
                        IconButton {
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingSmall
                            anchors.verticalCenter: parent.verticalCenter
                            iconSource: Icons.close
                            toolTipText: qsTr("Forget this server")
                            onClicked: page.signIn.forgetServer(savedServer.modelData.id)
                        }
                    }
                }

                SectionHeader {
                    visible: found.count > 0 || (page.signIn && page.signIn.discovering)
                    text: qsTr("On this network")
                }
                LoadingIndicator {
                    visible: page.signIn && page.signIn.discovering && found.count === 0
                }
                Repeater {
                    id: found
                    model: page.signIn ? page.signIn.discoveredServers : []
                    ListItem {
                        required property var modelData
                        width: serverColumn.width
                        text: modelData.name
                        subtitle: modelData.address
                        iconSource: Icons.search
                        onClicked: page.signIn.connectToServer(modelData.address)
                    }
                }

                InputField {
                    id: address
                    objectName: "serverAddress"
                    width: parent.width
                    focus: true
                    label: qsTr("Server address")
                    placeholderText: "http://192.168.1.10:8096"
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    onAccepted: chooser.connect()
                }
                Row {
                    spacing: Theme.spacing
                    ActionButton {
                        objectName: "connectButton"
                        primary: true
                        text: qsTr("Connect")
                        enabled: address.text.trim().length > 0
                        onClicked: chooser.connect()
                    }
                    ActionButton {
                        text: qsTr("Search again")
                        enabled: page.signIn && !page.signIn.discovering
                        onClicked: page.signIn.discover()
                    }
                }
            }
        }
    }

    Component {
        id: credentials
        FocusScope {
            id: form
            implicitHeight: credentialColumn.implicitHeight
            focus: true

            // Always sent: an empty name gets "Enter a user name." back.
            function submit() {
                page.signIn.signIn(user.text, password.text)
            }

            // Focus goes to the password only when the name is already known
            // (a saved server); it is decided once, never from the text, so
            // typing a name keeps the cursor in the name field. In TV mode
            // Quick Connect has it.
            Component.onCompleted: (page.quickConnectFirst ? quickConnectFirstButton
                                    : user.text.length > 0 ? password : user).forceActiveFocus()

            Column {
                id: credentialColumn
                width: parent.width
                spacing: Theme.spacing

                // TV mode: the recommended way, before the fields.
                Column {
                    width: parent.width
                    visible: page.quickConnectFirst
                    spacing: Theme.spacing
                    bottomPadding: Theme.spacingLarge

                    Text {
                        width: parent.width
                        text: qsTr("Recommended on a TV: sign in with Quick Connect, no typing needed. You get a code to enter in Jellyfin on your phone or computer.")
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        wrapMode: Text.Wrap
                    }
                    ActionButton {
                        id: quickConnectFirstButton
                        objectName: "quickConnectTvButton"
                        primary: true
                        text: qsTr("Sign in with Quick Connect")
                        onClicked: page.signIn.startQuickConnect()
                    }
                    SectionHeader {
                        width: parent.width
                        text: qsTr("Or with a password")
                    }
                }

                Text {
                    width: parent.width
                    text: page.signIn ? page.signIn.serverAddress : ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideMiddle
                }
                InputField {
                    id: user
                    objectName: "userName"
                    width: parent.width
                    label: qsTr("User name")
                    text: page.signIn ? page.signIn.userName : ""
                    inputMethodHints: Qt.ImhNoAutoUppercase
                    onAccepted: password.forceActiveFocus()
                }
                InputField {
                    id: password
                    objectName: "password"
                    width: parent.width
                    label: qsTr("Password")
                    password: true
                    onAccepted: form.submit()
                }
                Row {
                    spacing: Theme.spacing
                    ActionButton {
                        objectName: "signInButton"
                        primary: !page.quickConnectFirst
                        text: qsTr("Sign in")
                        onClicked: form.submit()
                    }
                    ActionButton {
                        objectName: "quickConnectButton"
                        visible: page.signIn && page.signIn.quickConnectAvailable && !page.quickConnectFirst
                        text: qsTr("Quick Connect")
                        onClicked: page.signIn.startQuickConnect()
                    }
                }
                ActionButton {
                    text: qsTr("Choose another server")
                    onClicked: page.signIn.back()
                }
            }
        }
    }

    Component {
        id: quickConnect
        FocusScope {
            implicitHeight: codeColumn.implicitHeight
            focus: true

            Column {
                id: codeColumn
                width: parent.width
                spacing: Theme.spacingLarge

                Text {
                    width: parent.width
                    text: qsTr("Enter this code in Jellyfin on a device where you are signed in: Settings, Quick Connect.")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
                Text {
                    objectName: "quickConnectCode"
                    text: page.signIn ? page.signIn.quickConnectCode : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeHeadline * 1.5
                    font.weight: Theme.fontWeightBold
                    font.letterSpacing: 6
                }
                ActionButton {
                    focus: true
                    text: qsTr("Cancel")
                    onClicked: page.signIn.cancelQuickConnect()
                }
            }
        }
    }
}
