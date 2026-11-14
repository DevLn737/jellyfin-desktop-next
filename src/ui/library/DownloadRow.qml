pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Library

// One download on the Downloads page: image, name, what it belongs to, its
// state ("4.1 of 9.8 GiB", "Paused", why it failed) with a progress bar,
// and its buttons. In the queue: pause or continue (try again after a
// failure), move up, move down, cancel; downloaded: play, delete.
//
// Keyboard and gamepad: Up and Down move between rows (the list), Enter
// does the main thing (pause or continue; play), Right and Left move along
// the row's buttons.
T.ItemDelegate {
    id: row

    required property var model
    required property int index
    // "queue" or "downloaded".
    property string kind: "queue"
    // The list's length, so the last row cannot move down.
    property int rowCount: 0

    signal deleteRequested(string itemId, string name)

    readonly property string itemId: model.itemId || ""
    readonly property string downloadState: model.downloadState || ""
    readonly property bool inQueue: kind === "queue"

    function mainAction() {
        if (!inQueue) {
            play()
        } else if (downloadState === "paused") {
            Downloads.resume(itemId)
        } else if (downloadState === "failed") {
            Downloads.retry(itemId)
        } else {
            Downloads.pause(itemId)
        }
    }

    function play() {
        if (model.type === "Audio") {
            if (!Downloads.playTrack(itemId))
                Library.playTrack(itemId, model.albumId || "")
        } else {
            Library.play(itemId, model.played ? 0 : (model.playbackPositionTicks || 0))
        }
    }

    objectName: "downloadRow_" + itemId
    implicitHeight: Math.max(64, captions.implicitHeight) + 2 * Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: Theme.spacingSmall
    Accessible.name: (model.name || "") + ", " + (model.statusText || "")

    Keys.onReturnPressed: row.mainAction()
    Keys.onEnterPressed: row.mainAction()
    Keys.onRightPressed: (event) => {
        const first = buttons.firstVisible()
        if (first) first.forceActiveFocus(Qt.TabFocusReason)
        else event.accepted = false
    }
    onClicked: mainAction()

    background: Rectangle {
        radius: Theme.radius
        color: row.hovered || row.visualFocus ? Theme.surfaceHover : "transparent"
        FocusFrame { shown: row.visualFocus }
    }

    contentItem: Item {
        Rectangle {
            id: thumb
            width: 96
            height: 64
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.radius
            color: Theme.surfaceRaised
            clip: true
            Image {
                id: image
                anchors.fill: parent
                source: row.model.imageUrl || ""
                sourceSize.width: 192
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Text {
                anchors.centerIn: parent
                visible: image.status !== Image.Ready
                text: (row.model.name || "?").charAt(0).toUpperCase()
                color: Theme.textDisabled
                font.pixelSize: Theme.fontSizeLarge
            }
            DownloadBadge {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 4
                size: 20
                visible: row.inQueue
                downloadState: row.downloadState === "queued" && row.model.activity !== "idle" ? "downloading"
                             : row.downloadState === "queued" && row.model.activity === "idle" ? "queued"
                             : row.downloadState
                progress: row.model.downloadProgress || 0
            }
        }

        Column {
            id: captions
            anchors.left: thumb.right
            anchors.leftMargin: Theme.spacing
            anchors.right: buttons.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTiny

            Text {
                width: parent.width
                text: row.model.name || ""
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Theme.fontWeightBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: row.model.subtitle || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
            Text {
                objectName: "downloadStatus"
                width: parent.width
                text: row.model.statusText || ""
                color: row.downloadState === "failed" ? Theme.error : Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
            Rectangle {
                objectName: "downloadProgressBar"
                visible: row.inQueue
                width: parent.width
                height: 4
                radius: 2
                color: Theme.surfaceRaised
                Rectangle {
                    width: parent.width * Math.max(0, Math.min(1, row.model.downloadProgress || 0))
                    height: parent.height
                    radius: 2
                    color: Theme.progress
                }
            }
        }

        Row {
            id: buttons
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingTiny

            function firstVisible() {
                for (let i = 0; i < children.length; ++i)
                    if (children[i].visible && children[i].enabled) return children[i]
                return null
            }

            // Left and Right walk the buttons; Left from the first goes back
            // to the row.
            function step(from, delta) {
                const list = []
                for (let i = 0; i < children.length; ++i)
                    if (children[i].visible && children[i].enabled) list.push(children[i])
                const at = list.indexOf(from) + delta
                if (at < 0) row.forceActiveFocus(Qt.TabFocusReason)
                else if (at < list.length) list[at].forceActiveFocus(Qt.TabFocusReason)
            }

            IconButton {
                id: b_pauseButton
                objectName: "pauseButton"
                visible: row.inQueue && row.downloadState !== "paused" && row.downloadState !== "failed"
                iconSource: Icons.pause
                toolTipText: qsTr("Pause")
                onClicked: Downloads.pause(row.itemId)
                Keys.onLeftPressed: buttons.step(b_pauseButton, -1)
                Keys.onRightPressed: buttons.step(b_pauseButton, 1)
            }
            IconButton {
                id: b_resumeButton
                objectName: "resumeButton"
                visible: row.inQueue && row.downloadState === "paused"
                iconSource: Icons.play
                toolTipText: qsTr("Continue")
                onClicked: Downloads.resume(row.itemId)
                Keys.onLeftPressed: buttons.step(b_resumeButton, -1)
                Keys.onRightPressed: buttons.step(b_resumeButton, 1)
            }
            IconButton {
                id: b_retryButton
                objectName: "retryButton"
                visible: row.inQueue && row.downloadState === "failed"
                iconSource: Icons.retry
                toolTipText: qsTr("Try again")
                onClicked: Downloads.retry(row.itemId)
                Keys.onLeftPressed: buttons.step(b_retryButton, -1)
                Keys.onRightPressed: buttons.step(b_retryButton, 1)
            }
            IconButton {
                id: b_moveUpButton
                objectName: "moveUpButton"
                visible: row.inQueue
                enabled: row.index > 0
                iconSource: Icons.up
                toolTipText: qsTr("Move up")
                onClicked: Downloads.moveUp(row.itemId)
                Keys.onLeftPressed: buttons.step(b_moveUpButton, -1)
                Keys.onRightPressed: buttons.step(b_moveUpButton, 1)
            }
            IconButton {
                id: b_moveDownButton
                objectName: "moveDownButton"
                visible: row.inQueue
                enabled: row.index < row.rowCount - 1
                iconSource: Icons.down
                toolTipText: qsTr("Move down")
                onClicked: Downloads.moveDown(row.itemId)
                Keys.onLeftPressed: buttons.step(b_moveDownButton, -1)
                Keys.onRightPressed: buttons.step(b_moveDownButton, 1)
            }
            IconButton {
                id: b_cancelButton
                objectName: "cancelButton"
                visible: row.inQueue
                iconSource: Icons.close
                toolTipText: qsTr("Cancel download")
                onClicked: Downloads.cancel(row.itemId)
                Keys.onLeftPressed: buttons.step(b_cancelButton, -1)
                Keys.onRightPressed: buttons.step(b_cancelButton, 1)
            }
            IconButton {
                id: b_playDownloadButton
                objectName: "playDownloadButton"
                visible: !row.inQueue
                iconSource: Icons.play
                toolTipText: qsTr("Play")
                onClicked: row.play()
                Keys.onLeftPressed: buttons.step(b_playDownloadButton, -1)
                Keys.onRightPressed: buttons.step(b_playDownloadButton, 1)
            }
            IconButton {
                id: b_deleteButton
                objectName: "deleteButton"
                visible: !row.inQueue
                iconSource: Icons.remove
                toolTipText: qsTr("Delete download")
                onClicked: row.deleteRequested(row.itemId, row.model.name || "")
                Keys.onLeftPressed: buttons.step(b_deleteButton, -1)
                Keys.onRightPressed: buttons.step(b_deleteButton, 1)
            }
        }
    }
}
