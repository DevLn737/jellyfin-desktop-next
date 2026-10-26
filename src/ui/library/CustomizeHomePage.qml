pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// "Customize home": every row of the home screen, with a switch to show or
// hide it and buttons to move it. Rows that have nothing to show right now
// stay in the list, so they can be arranged before they appear.
RoutePage {
    id: page

    readonly property HomeModel home: Library.home

    title: qsTr("Customize home")

    Heading {
        id: heading
        x: Theme.pageMargin
        y: Theme.spacingLarge
        text: page.title
    }

    ListView {
        id: list
        objectName: "homeSectionList"
        anchors.top: heading.bottom
        anchors.topMargin: Theme.spacingLarge
        anchors.bottom: footer.top
        anchors.left: parent.left
        anchors.right: parent.right
        leftMargin: Theme.pageMargin
        rightMargin: Theme.pageMargin
        clip: true
        focus: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        spacing: Theme.spacingTiny
        model: page.home ? page.home.sectionList : []

        delegate: FocusScope {
            id: row
            required property var modelData
            required property int index
            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            height: Theme.controlHeight + Theme.spacingSmall
            activeFocusOnTab: true

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius
                color: row.activeFocus ? Theme.surfaceHover : Theme.surface
            }

            Row {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacing
                anchors.rightMargin: Theme.spacing
                spacing: Theme.spacing

                ToggleSwitch {
                    objectName: "sectionVisible_" + row.modelData.id
                    anchors.verticalCenter: parent.verticalCenter
                    focus: true
                    checked: row.modelData.visible
                    Accessible.name: qsTr("Show %1").arg(row.modelData.title)
                    onToggled: if (page.home) page.home.setSectionVisible(row.modelData.id, checked)
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 3 * Theme.controlHeight - 3 * parent.spacing
                    text: row.modelData.title
                    color: row.modelData.visible ? Theme.text : Theme.textDisabled
                    font.pixelSize: Theme.fontSizeBody
                    elide: Text.ElideRight
                }
                IconButton {
                    objectName: "sectionUp_" + row.modelData.id
                    anchors.verticalCenter: parent.verticalCenter
                    iconSource: Icons.up
                    toolTipText: qsTr("Move up")
                    enabled: row.index > 0
                    onClicked: page.home.moveSection(row.index, row.index - 1)
                }
                IconButton {
                    objectName: "sectionDown_" + row.modelData.id
                    anchors.verticalCenter: parent.verticalCenter
                    iconSource: Icons.down
                    toolTipText: qsTr("Move down")
                    enabled: row.index < list.count - 1
                    onClicked: page.home.moveSection(row.index, row.index + 1)
                }
            }
        }
    }

    Row {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMargin
        anchors.bottomMargin: Theme.spacingLarge
        height: Theme.controlHeight + Theme.spacingLarge
        spacing: Theme.spacing

        ActionButton {
            objectName: "resetHomeSections"
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("Restore defaults")
            onClicked: if (page.home) page.home.resetArrangement()
        }
        ActionButton {
            objectName: "doneCustomizing"
            anchors.verticalCenter: parent.verticalCenter
            primary: true
            text: qsTr("Done")
            onClicked: Navigator.back()
        }
    }
}
