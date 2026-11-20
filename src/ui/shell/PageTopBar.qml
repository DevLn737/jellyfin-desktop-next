pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The strip at the top of a page: Back and where the page is (`crumbs`,
// each { text, route }, the last one the page itself) on the left; search
// (the command palette) and, when `moreEntries` has any, a "more" menu on
// the right. A page that has it sets `hasTopBar: true` on itself, so the
// sidebar leaves out its own Back.
Item {
    id: bar

    property var crumbs: []
    property var moreEntries: []
    signal moreActivated(int index)

    implicitHeight: Theme.controlHeight

    Row {
        id: left
        anchors.left: parent.left
        anchors.right: right.left
        anchors.rightMargin: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacing

        IconButton {
            objectName: "topBarBack"
            square: true
            // The navigation bar has Back now.
            visible: false
            iconSource: Icons.back
            toolTipText: qsTr("Back")
            onClicked: Navigator.back()
        }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall
            Repeater {
                model: bar.crumbs
                Row {
                    id: crumb
                    required property var modelData
                    required property int index
                    readonly property bool last: index === bar.crumbs.length - 1
                    spacing: Theme.spacingSmall
                    Text {
                        visible: crumb.index > 0
                        text: "/"
                        color: Theme.textDisabled
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    Text {
                        objectName: "crumb_" + crumb.index
                        text: crumb.modelData.text || ""
                        color: crumb.last ? Theme.text : link.hovered ? Theme.text : Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, crumb.last ? 420 : 240)
                        HoverHandler {
                            id: link
                            enabled: !crumb.last && (crumb.modelData.route || "").length > 0
                            cursorShape: Qt.PointingHandCursor
                        }
                        TapHandler {
                            enabled: link.enabled
                            onTapped: Navigator.navigate(crumb.modelData.route)
                        }
                    }
                }
            }
        }
    }

    Row {
        id: right
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingSmall

        IconButton {
            objectName: "topBarSearch"
            square: true
            iconSource: Icons.search
            toolTipText: qsTr("Search the library")
            onClicked: CommandRegistry.requestPalette()
        }
        IconButton {
            id: moreButton
            objectName: "topBarMore"
            square: true
            visible: bar.moreEntries.length > 0
            iconSource: Icons.more
            toolTipText: qsTr("More")
            toolTipSuppressed: moreMenu.visible
            onClicked: moreMenu.open()
        }
    }

    PopupMenu {
        id: moreMenu
        objectName: "topBarMenu"
        anchorItem: moreButton
        alignRight: true
        entries: bar.moreEntries
        onActivated: (index) => bar.moreActivated(index)
    }
}
