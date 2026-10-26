import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Main navigation on the left. Collapses to icons on narrow windows.
FocusScope {
    id: sidebar

    property bool collapsed: false
    // The button that opens the names over the page (compact windows).
    property bool menuButton: false
    signal menuToggled()
    // A destination was chosen.
    signal chosen()
    // Registered by the screens' modules (Navigator.registerRoute).
    readonly property var entries: Navigator.sidebarEntries
    // An entry's icon is a name from Icons or a file URL.
    function iconFor(name) { return Icons[name] !== undefined ? Icons[name] : name }

    width: collapsed ? Theme.sidebarCollapsedWidth : Theme.sidebarWidth
    activeFocusOnTab: true
    Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easing } }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    Column {
        anchors.fill: parent
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

        Item { width: 1; height: Theme.spacingSmall }

        Repeater {
            model: sidebar.entries
            ListItem {
                required property var modelData
                objectName: "nav_" + modelData.route
                width: parent.width
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
            }
        }
    }
}
