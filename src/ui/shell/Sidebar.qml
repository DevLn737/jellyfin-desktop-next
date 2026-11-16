import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// Main navigation on the left, a glass panel: the app's sign and name,
// destinations, the libraries with their sizes, then Settings and the
// account card (name, server and address) at the bottom. Back shows here
// only for pages without a top bar of their own (`backVisible`). Collapses
// to an icon rail on narrow windows.
FocusScope {
    id: sidebar

    property bool collapsed: false
    // The button that opens the names over the page (compact windows).
    property bool menuButton: false
    signal menuToggled()
    // A destination was chosen.
    signal chosen()
    // The user's libraries (an item list model with itemId, name and type
    // roles) shown under the destinations, and the account line at the
    // bottom (the server's name). Both are optional.
    property var libraries: null
    property string accountName
    // Under the name: the server and its address ("Home · jellyfin.local").
    property string accountDetail
    property bool backVisible: true
    signal libraryChosen(string itemId, string type)
    // "Sign out" in the account menu, confirmed.
    signal signOutRequested()
    // Registered by the screens' modules (Navigator.registerRoute); Settings
    // has its own place at the bottom.
    readonly property var entries: Navigator.sidebarEntries.filter((entry) => entry.route !== "settings")
    readonly property var settingsEntry: Navigator.sidebarEntries.find((entry) => entry.route === "settings")
    // An entry's icon is a name from Icons or a file URL.
    function iconFor(name) { return Icons[name] !== undefined ? Icons[name] : name }
    // The Menu key: focus on the entry of the current page (or the first).
    function focusCurrentEntry() {
        let target = null
        for (let i = 0; i < destinations.count; ++i) {
            const entry = destinations.itemAt(i)
            if (entry && (target === null || entry.highlighted))
                target = entry
            if (entry && entry.highlighted)
                break
        }
        if (target)
            target.forceActiveFocus(Qt.TabFocusReason)
        else
            sidebar.forceActiveFocus(Qt.TabFocusReason)
    }

    width: collapsed ? Theme.sidebarCollapsedWidth : Theme.sidebarWidth
    activeFocusOnTab: true
    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelFill
        // The edge towards the page.
        Rectangle {
            anchors.right: parent.right
            width: 1
            height: parent.height
            color: Theme.glassBorder
        }
    }

    Column {
        id: header
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacing
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        spacing: Theme.spacingTiny

        // The app's sign and name.
        Item {
            objectName: "sidebarBrand"
            width: parent.width
            height: Theme.controlHeight
            Rectangle {
                id: brandSign
                x: sidebar.collapsed ? (parent.width - width) / 2 : Theme.spacing - Theme.spacingTiny
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 28
                radius: Theme.radiusSmall + 2
                color: Qt.alpha(Theme.accent, 0.14)
                border.width: 1
                border.color: Qt.alpha(Theme.accent, 0.5)
                Icon {
                    anchors.centerIn: parent
                    source: Icons.play
                    size: 14
                    color: Theme.accent
                }
            }
            Text {
                visible: !sidebar.collapsed
                anchors.left: brandSign.right
                anchors.leftMargin: Theme.spacingSmall + 2
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Media library")
                color: Theme.text
                font.pixelSize: Math.round(Theme.fontSizeBody * 1.2)
                font.weight: Theme.fontWeightBold
            }
        }

        IconButton {
            objectName: "backButton"
            visible: sidebar.backVisible
            iconSource: Icons.back
            toolTipText: qsTr("Back")
            enabled: Navigator.canGoBack
            x: (sidebar.collapsed ? (parent.width - width) / 2 : Theme.spacingTiny)
            onClicked: Navigator.back()
        }

        IconButton {
            objectName: "menuButton"
            visible: sidebar.menuButton
            iconSource: Icons.menu
            toolTipText: qsTr("Menu")
            x: (sidebar.collapsed ? (parent.width - width) / 2 : Theme.spacingTiny)
            onClicked: sidebar.menuToggled()
        }
    }

    Flickable {
        SmoothWheel {}
        id: scroller
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingSmall
        anchors.bottom: settingsItem.top
        anchors.bottomMargin: Theme.spacingSmall
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        contentWidth: width
        contentHeight: nav.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: nav
            width: scroller.width
            spacing: Theme.spacingTiny

            Repeater {
                id: destinations
                model: sidebar.entries
                ListItem {
                    required property var modelData
                    objectName: "nav_" + modelData.route
                    width: nav.width
                    text: sidebar.collapsed ? "" : modelData.text
                    iconSource: sidebar.iconFor(modelData.icon)
                    highlighted: Navigator.currentName === modelData.route
                    Accessible.name: modelData.text
                    focus: index === 0
                    required property int index
                    onClicked: {
                        Navigator.navigate(modelData.route)
                        sidebar.chosen()
                    }
                    // The active entry: a thin bar of the accent colour.
                    Rectangle {
                        visible: parent.highlighted
                        z: 10
                        x: 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: parent.height - Theme.spacingSmall * 2
                        radius: 1
                        color: Theme.accent
                    }
                }
            }

            Item { width: 1; height: Theme.spacingSmall; visible: libs.count > 0 }

            Text {
                objectName: "librariesLabel"
                visible: libs.count > 0 && !sidebar.collapsed
                leftPadding: Theme.spacing
                text: qsTr("Libraries").toUpperCase()
                color: Theme.textDisabled
                font.pixelSize: Theme.fontSizeSmall - 1
                font.weight: Theme.fontWeightBold
                font.letterSpacing: 1.2
            }

            Repeater {
                id: libs
                model: sidebar.libraries
                ListItem {
                    id: libraryEntry
                    required property var model
                    objectName: "library_" + (model.itemId || "")
                    width: nav.width
                    text: sidebar.collapsed ? "" : (model.name || "")
                    trailingText: sidebar.collapsed || !(model.childCount > 0) ? "" : String(model.childCount)
                    highlighted: Navigator.currentRoute === "library/" + (model.itemId || "")
                    iconSource: Icons.library
                    Accessible.name: model.name || ""
                    onClicked: {
                        sidebar.libraryChosen(model.itemId || "", model.type || "")
                        sidebar.chosen()
                    }
                }
            }
        }
    }

    // Settings, over the account card.
    ListItem {
        id: settingsItem
        objectName: "nav_settings"
        anchors.bottom: gamepadSign.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        visible: sidebar.settingsEntry !== undefined
        height: visible ? implicitHeight : 0
        text: sidebar.collapsed || !visible ? "" : sidebar.settingsEntry.text
        iconSource: visible ? sidebar.iconFor(sidebar.settingsEntry.icon) : ""
        highlighted: Navigator.currentName === "settings"
        Accessible.name: visible ? sidebar.settingsEntry.text : ""
        onClicked: {
            Navigator.navigate("settings")
            sidebar.chosen()
        }
    }

    // A gamepad is connected: its sign and name over the account line.
    // Only a sign; the gamepad is checked in Settings > Gamepad.
    Item {
        id: gamepadSign
        objectName: "gamepadSign"
        anchors.bottom: account.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        visible: GamepadStatus.connected
        height: visible ? Math.max(Theme.iconSize, gamepadName.implicitHeight) + Theme.spacingSmall : 0
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Gamepad connected: %1").arg(GamepadStatus.controllerName)

        Icon {
            id: gamepadIcon
            x: sidebar.collapsed ? (parent.width - width) / 2 : Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            source: Icons.gamepad
            color: Theme.textSecondary
        }
        Text {
            id: gamepadName
            visible: !sidebar.collapsed
            anchors.left: gamepadIcon.right
            anchors.leftMargin: Theme.spacingSmall
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: GamepadStatus.controllerName
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
        }
    }

    // The account card: the user's initial, name and server; choosing it
    // opens a small menu: Settings, and Sign out after the same question the
    // settings page asks.
    T.ItemDelegate {
        id: account
        objectName: "sidebarAccount"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSmall
        visible: sidebar.accountName.length > 0
        height: visible ? 52 : 0
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.name: sidebar.accountName
        Keys.onReturnPressed: account.clicked()
        Keys.onEnterPressed: account.clicked()
        onClicked: accountMenu.open()

        background: Rectangle {
            radius: Theme.radius
            color: account.hovered || account.down ? Theme.surfaceHover : Theme.surface
            border.width: sidebar.collapsed ? 0 : 1
            border.color: Theme.glassBorder
            FocusFrame { shown: account.visualFocus }
        }
        contentItem: Item {
            Rectangle {
                id: avatar
                x: sidebar.collapsed ? (parent.width - width) / 2 : Theme.spacingSmall
                anchors.verticalCenter: parent.verticalCenter
                width: 30
                height: 30
                radius: 15
                color: Qt.alpha(Theme.accent, 0.25)
                Text {
                    anchors.centerIn: parent
                    text: sidebar.accountName.charAt(0).toUpperCase()
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.fontWeightBold
                }
            }
            Column {
                visible: !sidebar.collapsed
                anchors.left: avatar.right
                anchors.leftMargin: Theme.spacingSmall
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSmall
                anchors.verticalCenter: parent.verticalCenter
                Text {
                    objectName: "accountName"
                    width: parent.width
                    text: sidebar.accountName
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.fontWeightBold
                    elide: Text.ElideRight
                }
                Text {
                    objectName: "accountDetail"
                    width: parent.width
                    visible: text.length > 0
                    text: sidebar.accountDetail
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall - 1
                    elide: Text.ElideRight
                }
            }
        }
    }

    PopupMenu {
        id: accountMenu
        objectName: "accountMenu"
        anchorItem: account
        entries: [qsTr("Settings"), qsTr("Sign out")]
        onActivated: index => {
            if (index === 1) {
                signOutConfirm.open()
                return
            }
            Navigator.navigate("settings")
            sidebar.chosen()
        }
    }

    AppDialog {
        id: signOutConfirm
        objectName: "signOutConfirm"
        title: qsTr("Sign out")
        text: qsTr("Sign out of this server? Playback stops and the password is asked for again.")
        acceptText: qsTr("Sign out")
        destructive: true
        onAccepted: {
            sidebar.signOutRequested()
            sidebar.chosen()
        }
    }
}
