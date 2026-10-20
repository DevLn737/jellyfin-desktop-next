import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Main navigation on the left. Collapses to icons on narrow windows.
FocusScope {
    id: sidebar

    property bool collapsed: false
    readonly property var entries: [
        { route: "home", text: qsTr("Home"), icon: Icons.home },
        { route: "search", text: qsTr("Search"), icon: Icons.search },
        { route: "group", text: qsTr("Watch together"), icon: Icons.group },
        { route: "settings", text: qsTr("Settings"), icon: Icons.settings },
    ]

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

        Item { width: 1; height: Theme.spacingSmall }

        Repeater {
            model: sidebar.entries
            ListItem {
                required property var modelData
                objectName: "nav_" + modelData.route
                width: parent.width
                text: sidebar.collapsed ? "" : modelData.text
                iconSource: modelData.icon
                highlighted: Navigator.currentName === modelData.route
                Accessible.name: modelData.text
                focus: index === 0
                required property int index
                onClicked: Navigator.navigate(modelData.route)
            }
        }
    }
}
