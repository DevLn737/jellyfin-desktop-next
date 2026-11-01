import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Main navigation on the left, a glass panel: destinations, libraries and
// the account at the bottom. Collapses to an icon rail on narrow windows.
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
    signal libraryChosen(string itemId, string type)
    // Registered by the screens' modules (Navigator.registerRoute).
    readonly property var entries: Navigator.sidebarEntries
    // An entry's icon is a name from Icons or a file URL.
    function iconFor(name) { return Icons[name] !== undefined ? Icons[name] : name }

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

        IconButton {
            objectName: "backButton"
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
        anchors.bottom: account.top
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
                text: qsTr("Libraries")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Theme.fontWeightBold
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

    // The account line; choosing it opens a small menu (the first use of
    // PopupMenu).
    ListItem {
        id: account
        objectName: "sidebarAccount"
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: Theme.spacingSmall
        visible: sidebar.accountName.length > 0
        height: visible ? implicitHeight : 0
        text: sidebar.collapsed ? "" : sidebar.accountName
        iconSource: Icons.group
        Accessible.name: sidebar.accountName
        onClicked: accountMenu.open()
    }

    PopupMenu {
        id: accountMenu
        objectName: "accountMenu"
        anchorItem: account
        entries: [qsTr("Settings")]
        onActivated: index => {
            Navigator.navigate("settings")
            sidebar.chosen()
        }
    }
}
