import QtQuick
import Jfd.Ui.Theme

// A download's state on a card or a row: a ring that fills while it
// downloads, the download arrow while it waits or is paused, a check once
// it is on disk, the error mark when it failed. `state` is "" (nothing:
// hidden), "queued", "downloading", "paused", "failed" or "complete";
// `progress` 0..1.
Item {
    id: badge

    property string downloadState
    property real progress: 0
    property int size: 26
    readonly property var colors: Theme.colorsFor(badge)

    width: size
    height: size
    visible: downloadState.length > 0
    Accessible.role: Accessible.StaticText
    Accessible.name: downloadState === "complete" ? qsTr("Downloaded")
                   : downloadState === "downloading" ? qsTr("Downloading, %1 %").arg(Math.round(progress * 100))
                   : downloadState === "queued" ? qsTr("Waiting to download")
                   : downloadState === "paused" ? qsTr("Download paused")
                   : downloadState === "failed" ? qsTr("Download failed") : ""

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: badge.downloadState === "complete" ? badge.colors.accent
             : badge.downloadState === "failed" ? badge.colors.error : badge.colors.scrim
    }

    // The ring: a full circle under the part done.
    Canvas {
        id: ring
        anchors.fill: parent
        visible: badge.downloadState === "downloading" || badge.downloadState === "paused"
        readonly property real done: Math.max(0, Math.min(1, badge.progress))
        readonly property color trackColor: Qt.alpha(Overlay.text, 0.3)
        readonly property color doneColor: badge.colors.progress
        onDoneChanged: requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = width / 2 - 2
            ctx.lineWidth = 2.5
            ctx.strokeStyle = trackColor
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
            ctx.stroke()
            if (done > 0) {
                ctx.strokeStyle = doneColor
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * done)
                ctx.stroke()
            }
        }
    }

    Icon {
        anchors.centerIn: parent
        size: Math.round(badge.size * 0.6)
        source: badge.downloadState === "complete" ? Icons.downloaded
              : badge.downloadState === "failed" ? Icons.error
              : badge.downloadState === "paused" ? Icons.pause : Icons.download
        color: badge.downloadState === "complete" ? badge.colors.accentText : Overlay.text
    }
}
