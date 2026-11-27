pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
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

    // TV mode or a connected gamepad: Quick Connect leads the password form
    // (a code is quicker than typing a password with a pad). Not the last
    // input: the form must not change under a key just pressed.
    readonly property bool quickConnectFirst: (Theme.tvMode || GamepadStatus.connected) && signIn !== null && signIn.quickConnectAvailable

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

    // The network is searched once the server choice shows: at once, or
    // after a saved sign-in turned out to have no token.
    property bool discoveryStarted: false
    function discoverOnce() {
        if (discoveryStarted || !signIn || step !== SignIn.ChooseServer)
            return
        discoveryStarted = true
        signIn.discover()
    }
    Component.onCompleted: discoverOnce()
    onStepChanged: discoverOnce()

    // A dark gradient lit by two soft glows in the accent's colours.
    Canvas {
        id: glow
        anchors.fill: parent
        readonly property color first: Theme.accent
        readonly property color second: Qt.hsla((Theme.accent.hslHue + 0.25) % 1, 0.5, 0.5, 1)
        onFirstChanged: requestPaint()
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const spots = [[0.22, 0.25, first], [0.8, 0.8, second]]
            for (const spot of spots) {
                const x = width * spot[0], y = height * spot[1], r = Math.max(width, height) * 0.55
                const g = ctx.createRadialGradient(x, y, 0, x, y, r)
                g.addColorStop(0, Qt.alpha(spot[2], 0.16))
                g.addColorStop(1, Qt.alpha(spot[2], 0))
                ctx.fillStyle = g
                ctx.fillRect(0, 0, width, height)
            }
        }
    }

    Flickable {
        SmoothWheel {}
        anchors.fill: parent
        contentHeight: Math.max(height, column.implicitHeight + 2 * Theme.spacingHuge + 2 * cardPadding)
        readonly property int cardPadding: Theme.tvMode ? 48 : 40
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // The glass card around the form.
        Rectangle {
            objectName: "loginCard"
            x: column.x - parent.parent.cardPadding
            y: column.y - parent.parent.cardPadding
            width: column.width + 2 * parent.parent.cardPadding
            height: column.implicitHeight + 2 * parent.parent.cardPadding
            radius: Theme.radiusLarge + 10
            color: Theme.panelFill
            border.width: 1
            border.color: Theme.glassBorder
        }

        Column {
            id: column
            objectName: "loginColumn"
            width: Math.min(parent.width - 2 * Theme.spacingHuge - 2 * parent.parent.cardPadding, Theme.tvMode ? 760 : 420)
            x: (parent.width - width) / 2
            // The error line does not count: the form stays where it was
            // when a message appears above it. Never in a TV's outer 5 %.
            y: Math.max(Theme.spacingHuge, Math.round(page.height * Theme.safeMargin),
                        (page.height - implicitHeight + (loginError.visible ? loginError.height + spacing : 0)) / 3)
            spacing: Theme.spacingLarge

            // The app's mark, as in the top bar.
            AppLogo {
                objectName: "loginLogo"
                anchors.horizontalCenter: parent.horizontalCenter
                size: Theme.tvMode ? 72 : 56
            }

            Column {
                width: parent.width
                spacing: Theme.spacingTiny
                Heading {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    text: page.step === SignIn.ChooseServer ? qsTr("Connect to a server")
                        : page.step === SignIn.QuickConnect ? qsTr("Quick Connect")
                        : page.step === SignIn.Restoring ? qsTr("Signing in")
                        : qsTr("Who's watching?")
                }
                Text {
                    width: parent.width
                    visible: page.step === SignIn.Credentials
                    horizontalAlignment: Text.AlignHCenter
                    text: page.signIn && page.signIn.publicUsers.length > 0
                          ? qsTr("Choose a profile or sign in") : qsTr("Sign in to your account")
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
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

                // The server, and the way to another.
                Rectangle {
                    objectName: "loginServer"
                    width: parent.width
                    height: Theme.controlHeight + 8
                    radius: Theme.radius + 2
                    color: Qt.alpha(Theme.text, 0.05)
                    Rectangle {
                        id: serverDot
                        x: Theme.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        width: 8
                        height: 8
                        radius: 4
                        color: Theme.success
                    }
                    Text {
                        anchors.left: serverDot.right
                        anchors.leftMargin: Theme.spacingSmall
                        anchors.right: changeServer.left
                        anchors.rightMargin: Theme.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.signIn ? [page.signIn.serverName, page.signIn.serverAddress].filter((t) => t.length > 0).join(" · ") : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeSmall + 1
                        font.weight: Font.Bold
                        elide: Text.ElideMiddle
                    }
                    T.AbstractButton {
                        id: changeServer
                        objectName: "changeServer"
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacingSmall
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: implicitContentWidth + leftPadding + rightPadding
                        implicitHeight: Theme.controlHeight
                        padding: Theme.spacingSmall
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        text: qsTr("Change")
                        Accessible.name: qsTr("Choose another server")
                        Keys.onReturnPressed: changeServer.clicked()
                        Keys.onEnterPressed: changeServer.clicked()
                        onClicked: page.signIn.back()
                        contentItem: Text {
                            text: changeServer.text
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeSmall + 1
                            font.weight: Font.ExtraBold
                            font.underline: changeServer.hovered
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: FocusFrame { shown: changeServer.visualFocus }
                    }
                }

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

                // The server's users: a click fills the name and asks for the
                // password; "Other" types another name.
                Flow {
                    objectName: "loginUsers"
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Math.min(parent.width, usersRepeater.count * 88 + 88)
                    visible: usersRepeater.count > 0
                    spacing: 0
                    topPadding: Theme.spacingSmall
                    bottomPadding: Theme.spacingSmall
                    Repeater {
                        id: usersRepeater
                        model: page.signIn ? page.signIn.publicUsers : []
                        UserChoice {
                            required property var modelData
                            objectName: "loginUser_" + modelData.name
                            name: modelData.name
                            selected: user.text === modelData.name
                            onClicked: {
                                user.text = modelData.name
                                password.text = ""
                                password.forceActiveFocus()
                            }
                        }
                    }
                    UserChoice {
                        objectName: "loginUserOther"
                        name: qsTr("Other")
                        other: true
                        onClicked: {
                            user.text = ""
                            user.forceActiveFocus()
                        }
                    }
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
            }
        }
    }

    // A user of the server: a round initial and the name under it.
    component UserChoice: T.AbstractButton {
        id: choice
        property string name
        property bool selected: false
        property bool other: false
        readonly property real hue: name.split("").reduce((h, c) => (h * 31 + c.charCodeAt(0)) % 360, 7) / 360
        implicitWidth: 88
        implicitHeight: avatar.height + label.implicitHeight + 14
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.name: name
        Keys.onReturnPressed: choice.clicked()
        Keys.onEnterPressed: choice.clicked()
        Rectangle {
            id: avatar
            anchors.horizontalCenter: parent.horizontalCenter
            y: 2
            width: 60
            height: 60
            radius: 30
            color: choice.other ? Qt.alpha(Theme.text, 0.08) : "transparent"
            gradient: choice.other ? null : fill
            border.width: choice.selected || choice.visualFocus ? 3 : 0
            border.color: choice.visualFocus ? Theme.focusRing : Theme.text
            scale: choice.hovered ? 1.05 : 1
            Behavior on scale { NumberAnimation { duration: 120 } }
            Gradient {
                id: fill
                GradientStop { position: 0; color: Qt.hsla(choice.hue, 0.5, 0.58, 1) }
                GradientStop { position: 1; color: Qt.hsla((choice.hue + 0.08) % 1, 0.5, 0.42, 1) }
            }
            Text {
                anchors.centerIn: parent
                text: choice.other ? "+" : choice.name.charAt(0).toUpperCase()
                color: choice.other ? Theme.textSecondary : "white"
                font.pixelSize: 24
                font.weight: Font.Bold
            }
        }
        Text {
            id: label
            anchors.top: avatar.bottom
            anchors.topMargin: 8
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: choice.name
            color: choice.selected ? Theme.text : Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall + 1
            font.weight: Font.Bold
            elide: Text.ElideRight
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
