pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Library

// "Download" on an item, a season, a series, an album or a playlist: asks
// the server what it takes, shows the total ("12 items, about 18.4 GiB")
// and whether it fits the space for downloads, lets the user pick the
// version of a film with several and, for a series or season, only the
// unwatched episodes; nothing is queued until "Download". The original
// file is downloaded (compressed copies come later). Keyboard: Tab moves,
// Enter downloads, Esc cancels.
AppDialog {
    id: dialog
    objectName: "downloadDialog"

    property string itemId
    property string itemType
    property string itemName
    // While the server is asked.
    property int requestId: 0
    property var summary: null
    property string errorText
    readonly property bool canChooseUnwatched: itemType === "Series" || itemType === "Season"
    readonly property bool ready: summary !== null
    readonly property bool hasVersions: ready && summary.versions.length > 1

    title: qsTr("Download")
    acceptEnabled: ready && summary.count > 0
    acceptText: ready && summary.count > 0 && !summary.fits ? qsTr("Download anyway") : qsTr("Download")
    width: Math.min(520, parent ? parent.width - 2 * Theme.spacingHuge : 520)

    function start(id, type, name) {
        itemId = id
        itemType = type
        itemName = name
        unwatched.checked = false
        ask()
        open()
    }

    function ask() {
        if (requestId !== 0) Downloads.discard(requestId)
        summary = null
        errorText = ""
        requestId = Downloads.prepare(itemId, canChooseUnwatched && unwatched.checked)
    }

    Connections {
        target: Downloads
        function onPrepared(id, result) { if (id === dialog.requestId) dialog.summary = result }
        function onPrepareFailed(id, text) { if (id === dialog.requestId) dialog.errorText = text }
    }

    onAccepted: {
        if (ready && summary.count > 0)
            Downloads.confirm(requestId, hasVersions ? versions.currentIndex : 0)
        requestId = 0
        summary = null
    }
    onRejected: {
        if (requestId !== 0) Downloads.discard(requestId)
        requestId = 0
        summary = null
    }

    contentItem: Column {
        spacing: Theme.spacing

        Text {
            objectName: "downloadTitle"
            width: parent.width
            visible: dialog.itemName.length > 0
            text: dialog.itemName
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            font.weight: Theme.fontWeightBold
            wrapMode: Text.Wrap
        }

        LoadingIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: !dialog.ready && dialog.errorText.length === 0
        }

        Text {
            objectName: "downloadError"
            width: parent.width
            visible: dialog.errorText.length > 0
            text: dialog.errorText
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "downloadSummary"
            width: parent.width
            visible: dialog.ready
            text: {
                if (!dialog.ready)
                    return ""
                const s = dialog.summary
                if (s.count === 0)
                    return s.alreadyCount > 0 ? qsTr("Everything here is already downloaded.")
                         : s.unavailableCount > 0 ? qsTr("Nothing here can be downloaded as a file.")
                         : qsTr("There is nothing here to download.")
                if (s.single)
                    return qsTr("One file, %1").arg(Downloads.formatBytes(s.bytes))
                return qsTr("%n item(s), about %1", "", s.count).arg(Downloads.formatBytes(s.bytes))
            }
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        Text {
            objectName: "downloadDetails"
            width: parent.width
            visible: text.length > 0
            text: {
                if (!dialog.ready)
                    return ""
                const parts = []
                if (dialog.summary.count > 0)
                    parts.push(qsTr("Quality: original, the file exactly as the server has it."))
                if (dialog.summary.alreadyCount > 0 && dialog.summary.count > 0)
                    parts.push(qsTr("%n already downloaded.", "", dialog.summary.alreadyCount))
                if (dialog.summary.unavailableCount > 0)
                    parts.push(qsTr("%n cannot be downloaded: a folder, a disc image or a stream.", "",
                                    dialog.summary.unavailableCount))
                return parts.join("\n")
            }
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
        }

        Row {
            spacing: Theme.spacing
            visible: dialog.hasVersions
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Version")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
            }
            ChoiceBox {
                id: versions
                objectName: "downloadVersion"
                width: 300
                Accessible.name: qsTr("Version")
                model: dialog.hasVersions
                    ? dialog.summary.versions.map((v, i) => ({ value: i, label: v.name + " · " + Downloads.formatBytes(v.bytes) }))
                    : []
                KeyNavigation.tab: unwatched.visible ? unwatched : null
            }
        }

        Row {
            spacing: Theme.spacing
            visible: dialog.canChooseUnwatched
            ToggleSwitch {
                id: unwatched
                objectName: "downloadUnwatched"
                Accessible.name: qsTr("Only unwatched episodes")
                onToggled: dialog.ask()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Only unwatched episodes")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                MouseArea {
                    anchors.fill: parent
                    onClicked: { unwatched.toggle(); dialog.ask() }
                }
            }
        }

        Text {
            objectName: "downloadSpace"
            width: parent.width
            visible: dialog.ready && dialog.summary.count > 0 && !dialog.summary.fits
            text: dialog.ready ? dialog.summary.spaceText + " " + qsTr("The download will wait until there is room.") : ""
            color: Theme.error
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.Wrap
        }
    }
}
