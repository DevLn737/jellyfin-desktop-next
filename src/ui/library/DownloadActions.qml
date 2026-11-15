pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The download buttons of an item, season, series, album or playlist page:
// "Download" (with its dialog), the download's state once there is one
// (which leads to the Downloads page), and "Delete download". Without the
// server's permission (EnableContentDownloading off) there is no
// "Download", and a line says why. Nothing at all without the offline
// module or for items that cannot be downloaded (libraries, people).
Row {
    id: actions

    property string itemId
    property string itemType
    property string itemName

    // `var` lists: a literal string list compiles to a QStringList, whose
    // indexOf (qsizetype) does not convert to int under -Wconversion.
    readonly property var groupTypes: ["Series", "Season", "MusicAlbum", "Playlist", "BoxSet"]
    readonly property var fileTypes: ["Movie", "Episode", "Video", "MusicVideo", "Audio", "AudioBook"]
    readonly property bool isGroup: groupTypes.indexOf(itemType) >= 0
    readonly property bool downloadable: isGroup || fileTypes.indexOf(itemType) >= 0
    readonly property string downloadState: Downloads.revision >= 0 && itemId.length > 0 ? Downloads.stateOf(itemId) : ""
    readonly property real progress: downloadState === "downloading" || downloadState === "paused"
        ? (Downloads.progressRevision >= 0 ? Downloads.progressOf(itemId) : 0) : 0
    // Downloaded or queued: "Delete" removes it (and stops it).
    readonly property bool hasFiles: downloadState.length > 0
    readonly property bool active: Downloads.available && Downloads.signedIn && downloadable && itemId.length > 0
    // Not the children's `visible`: that turns false with the row's own and
    // would keep the row hidden.
    readonly property bool showMain: active && (Downloads.allowed || (!isGroup && downloadState.length > 0))
    readonly property bool showDelete: active && hasFiles
    readonly property bool showNotice: active && Downloads.permissionKnown && !Downloads.allowed

    spacing: Theme.spacing
    visible: showMain || showDelete || showNotice

    ActionButton {
        id: mainButton
        objectName: "downloadButton"
        visible: actions.showMain
        iconSource: actions.downloadState === "complete" && !actions.isGroup ? Icons.downloaded : Icons.download
        text: {
            const s = actions.downloadState
            const percent = Math.round(actions.progress * 100)
            if (actions.isGroup) {
                if (s === "") return qsTr("Download")
                if (s === "complete") return qsTr("Download more")
                if (s === "failed") return qsTr("Download failed")
                return qsTr("Downloading, %1 %").arg(percent)
            }
            switch (s) {
            case "complete": return qsTr("Downloaded")
            case "downloading": return qsTr("Downloading, %1 %").arg(percent)
            case "queued": return qsTr("Waiting to download")
            case "paused": return qsTr("Download paused")
            case "failed": return qsTr("Download failed")
            default: return qsTr("Download")
            }
        }
        onClicked: {
            if (actions.isGroup || actions.downloadState === "")
                dialog.start(actions.itemId, actions.itemType, actions.itemName)
            else
                Navigator.navigate("downloads")
        }
    }

    ActionButton {
        id: deleteButton
        objectName: "deleteDownloadButton"
        visible: actions.showDelete
        iconSource: Icons.remove
        text: actions.isGroup ? qsTr("Delete downloads") : qsTr("Delete download")
        onClicked: confirmDelete.open()
    }

    Text {
        id: notice
        objectName: "downloadNotAllowed"
        anchors.verticalCenter: parent.verticalCenter
        visible: actions.showNotice
        text: qsTr("Downloading is turned off for your account on this server.")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeSmall
    }

    DownloadDialog { id: dialog }

    AppDialog {
        id: confirmDelete
        objectName: "confirmDeleteDownload"
        title: actions.isGroup ? qsTr("Delete downloads") : qsTr("Delete download")
        text: qsTr("Delete the downloaded files of \"%1\"? This frees %2. It stays on the server.")
            .arg(actions.itemName).arg(Downloads.formatBytes(Downloads.revision >= 0 ? Downloads.bytesOf(actions.itemId) : 0))
        acceptText: qsTr("Delete")
        destructive: true
        onAccepted: Downloads.remove(actions.itemId)
    }
}
