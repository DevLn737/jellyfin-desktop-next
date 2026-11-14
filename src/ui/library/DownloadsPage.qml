pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Downloads (route "downloads", in the sidebar): how much space they take,
// then two tabs. "Queue": what is downloading or waiting, with pause,
// continue, try again, reorder and cancel, and the whole queue's state
// (paused, the disk full, sign-in needed). "Downloaded": what is on disk,
// with a name filter, play and delete, "Delete watched" and "Delete all".
// Works without the server. Everything is reachable with the keyboard or
// a gamepad: Tab or Up/Down between the parts, Left/Right along a row.
RoutePage {
    id: page

    // "queue" or "downloaded"; the queue first while there is one.
    property string tab: Downloads.queue && Downloads.queue.count > 0 ? "queue" : "downloaded"
    readonly property DownloadListModel list: tab === "queue" ? Downloads.queue : Downloads.downloaded
    // Space: the limit when there is one, else what downloads use plus the
    // free space of the disk.
    readonly property real capacity: Downloads.limitBytes > 0 ? Downloads.limitBytes
        : (Downloads.freeBytes >= 0 ? Downloads.usedBytes + Downloads.freeBytes : 0)

    title: qsTr("Downloads")

    function deleteOne(itemId, name) {
        confirm.itemId = itemId
        confirm.mode = "one"
        confirm.title = qsTr("Delete download")
        confirm.text = qsTr("Delete the downloaded file of \"%1\"? This frees %2. It stays on the server.")
            .arg(name).arg(Downloads.formatBytes(Downloads.bytesOf(itemId)))
        confirm.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        Heading {
            Layout.fillWidth: true
            text: qsTr("Downloads")
        }

        StateMessage {
            objectName: "downloadsUnavailable"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            visible: !Downloads.available || !Downloads.signedIn
            kind: "empty"
            title: qsTr("Downloads are not available")
            description: qsTr("Sign in to a server to download.")
        }

        // The space bar.
        Column {
            objectName: "storageBar"
            Layout.fillWidth: true
            visible: Downloads.available && Downloads.signedIn
            spacing: Theme.spacingSmall
            Accessible.role: Accessible.ProgressBar
            Accessible.name: storageText.text

            Rectangle {
                width: parent.width
                height: 8
                radius: 4
                color: Theme.surfaceRaised
                Rectangle {
                    objectName: "storageUsed"
                    width: page.capacity > 0 ? parent.width * Math.min(1, Downloads.usedBytes / page.capacity) : 0
                    height: parent.height
                    radius: 4
                    color: Downloads.limitBytes > 0 && Downloads.usedBytes >= Downloads.limitBytes ? Theme.error : Theme.accent
                }
            }
            Text {
                id: storageText
                objectName: "storageText"
                width: parent.width
                text: {
                    const parts = []
                    parts.push(Downloads.limitBytes > 0
                        ? qsTr("Downloads use %1 of %2").arg(Downloads.formatBytes(Downloads.usedBytes)).arg(Downloads.formatBytes(Downloads.limitBytes))
                        : qsTr("Downloads use %1").arg(Downloads.formatBytes(Downloads.usedBytes)))
                    parts.push(Downloads.freeBytes >= 0
                        ? qsTr("%1 free on the disk").arg(Downloads.formatBytes(Downloads.freeBytes))
                        : qsTr("The download folder's disk is not available"))
                    return parts.join("  ·  ")
                }
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: Downloads.available && Downloads.signedIn
            spacing: Theme.spacingSmall

            ActionButton {
                id: queueTab
                objectName: "queueTab"
                focus: true
                primary: page.tab === "queue"
                text: qsTr("Queue (%1)").arg(Downloads.queue ? Downloads.queue.count : 0)
                onClicked: page.tab = "queue"
                KeyNavigation.right: downloadedTab
                KeyNavigation.down: toolbar.firstButton()
            }
            ActionButton {
                id: downloadedTab
                objectName: "downloadedTab"
                primary: page.tab === "downloaded"
                text: qsTr("Downloaded (%1)").arg(Downloads.downloaded ? Downloads.downloaded.count : 0)
                onClicked: page.tab = "downloaded"
                KeyNavigation.down: toolbar.firstButton()
            }
            Item { Layout.fillWidth: true }
        }

        // What applies to the whole list.
        RowLayout {
            id: toolbar
            Layout.fillWidth: true
            visible: Downloads.available && Downloads.signedIn
            spacing: Theme.spacingSmall

            function firstButton() {
                if (page.tab === "queue")
                    return resumeAllButton.visible ? resumeAllButton : pauseAllButton.visible ? pauseAllButton : list
                return filter.visible ? filter : list
            }

            Text {
                objectName: "queueProblem"
                Layout.fillWidth: true
                visible: page.tab === "queue" && Downloads.queueStopped
                text: Downloads.queueProblemText
                color: Downloads.queuePausedByUser ? Theme.textSecondary : Theme.error
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }
            ActionButton {
                id: resumeAllButton
                objectName: "resumeAllButton"
                visible: page.tab === "queue" && Downloads.queueStopped
                primary: true
                iconSource: Icons.play
                text: qsTr("Continue")
                onClicked: Downloads.resumeAll()
                KeyNavigation.down: list
            }
            Item {
                Layout.fillWidth: true
                visible: page.tab === "queue" && !Downloads.queueStopped
            }
            ActionButton {
                id: pauseAllButton
                objectName: "pauseAllButton"
                visible: page.tab === "queue" && !Downloads.queueStopped && Downloads.queue && Downloads.queue.count > 0
                iconSource: Icons.pause
                text: qsTr("Pause all")
                onClicked: Downloads.pauseAll()
                KeyNavigation.down: list
            }

            InputField {
                id: filter
                objectName: "downloadsFilter"
                Layout.fillWidth: true
                visible: page.tab === "downloaded" && Downloads.downloaded && (Downloads.downloaded.count > 0 || text.length > 0)
                placeholderText: qsTr("Filter by name")
                Accessible.name: qsTr("Filter by name")
                onTextChanged: if (Downloads.downloaded) Downloads.downloaded.filterText = text
                KeyNavigation.right: deleteWatchedButton.visible ? deleteWatchedButton : deleteAllButton
                KeyNavigation.down: list
            }
            ActionButton {
                id: deleteWatchedButton
                objectName: "deleteWatchedButton"
                visible: page.tab === "downloaded" && Downloads.watchedBytes > 0
                text: qsTr("Delete watched")
                onClicked: {
                    confirm.mode = "watched"
                    confirm.title = qsTr("Delete watched downloads")
                    confirm.text = qsTr("Delete every watched download? This frees %1.").arg(Downloads.formatBytes(Downloads.watchedBytes))
                    confirm.open()
                }
                KeyNavigation.right: deleteAllButton
                KeyNavigation.down: list
            }
            ActionButton {
                id: deleteAllButton
                objectName: "deleteAllButton"
                visible: page.tab === "downloaded" && Downloads.hasDownloads
                iconSource: Icons.remove
                text: qsTr("Delete all")
                onClicked: {
                    confirm.mode = "all"
                    confirm.title = qsTr("Delete all downloads")
                    confirm.text = qsTr("Delete every download, the queue included? This frees %1. Everything stays on the server.")
                        .arg(Downloads.formatBytes(Downloads.usedBytes))
                    confirm.open()
                }
                KeyNavigation.down: list
            }
        }

        ListView {
            SmoothWheel {}
            id: list
            objectName: page.tab === "queue" ? "queueList" : "downloadedList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: Downloads.available && Downloads.signedIn
            clip: true
            spacing: Theme.spacingTiny
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            highlightMoveDuration: Theme.durationNormal
            model: page.list
            delegate: DownloadRow {
                width: ListView.view.width
                kind: page.tab
                rowCount: page.list ? page.list.count : 0
                focus: ListView.isCurrentItem
                onDeleteRequested: (itemId, name) => page.deleteOne(itemId, name)
            }
            // Up from the first row goes back to the toolbar.
            Keys.onUpPressed: (event) => {
                if (currentIndex <= 0) {
                    const target = toolbar.firstButton()
                    if (target && target !== list) target.forceActiveFocus(Qt.TabFocusReason)
                    else queueTab.forceActiveFocus(Qt.TabFocusReason)
                } else {
                    decrementCurrentIndex()
                }
            }

            StateMessage {
                objectName: "downloadsEmpty"
                anchors.centerIn: parent
                width: Math.min(parent.width, 420)
                visible: page.list !== null && page.list.count === 0
                kind: "empty"
                retryVisible: false
                title: page.tab === "queue" ? qsTr("Nothing is downloading")
                     : filter.text.length > 0 ? qsTr("No download matches") : qsTr("Nothing downloaded yet")
                description: page.tab === "queue" ? ""
                     : qsTr("Use Download on a film, series, album or playlist to keep it for watching without the server.")
            }
        }
    }

    AppDialog {
        id: confirm
        objectName: "confirmDownloadsDelete"
        property string mode
        property string itemId
        acceptText: qsTr("Delete")
        destructive: true
        onAccepted: {
            if (mode === "one") Downloads.remove(itemId)
            else if (mode === "watched") Downloads.removeWatched()
            else if (mode === "all") Downloads.removeAll()
        }
    }
}
