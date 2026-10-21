pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// An episode in a season list: still image with progress, "1. Name",
// run time and overview. Clicking opens the episode's page; the play
// button starts it.
T.ItemDelegate {
    id: row

    required property var model
    required property int index

    signal playClicked()

    implicitHeight: Math.max(still.height, captions.implicitHeight) + 2 * Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: Theme.spacingSmall
    Accessible.name: model.name || ""

    Keys.onReturnPressed: row.clicked()
    Keys.onEnterPressed: row.clicked()
    onClicked: if (model.itemId) Navigator.navigate("item/" + model.itemId)

    background: Rectangle {
        radius: Theme.radius
        color: row.hovered || row.visualFocus ? Theme.surfaceHover : "transparent"
        FocusFrame { shown: row.visualFocus }
    }

    contentItem: Item {
        Rectangle {
            id: still
            width: Theme.thumbWidth * 0.75
            height: Math.round(width / Theme.thumbAspect)
            radius: Theme.radius
            color: Theme.surfaceRaised
            clip: true

            Image {
                anchors.fill: parent
                source: Library.image(row.model.imageUrl)
                sourceSize.width: still.width * 2
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Rectangle {
                visible: (row.model.progress || 0) > 0
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: parent.width * (row.model.progress || 0)
                height: 4
                color: Theme.progress
            }
            Icon {
                visible: row.model.played === true
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingSmall
                source: Icons.check
                size: 18
            }
        }

        Column {
            id: captions
            anchors.left: still.right
            anchors.leftMargin: Theme.spacing
            anchors.right: play.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTiny

            Text {
                width: parent.width
                text: (row.model.subtitle ? row.model.subtitle + " · " : "") + (row.model.name || "")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Theme.fontWeightBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: Library.formatRunTime(row.model.runTimeTicks || 0)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: row.model.overview || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        IconButton {
            id: play
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconSource: Icons.play
            toolTipText: qsTr("Play")
            onClicked: row.playClicked()
        }
    }
}
