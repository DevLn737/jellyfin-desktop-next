import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Statistics for the curious: how the file plays (direct or transcoded and
// why), the video and audio, the buffer and the network, and in a room the
// delay to the server and how far we are from the others. Read from the
// player once a second while shown. The right-click menu on the video and
// the I key show and hide it.
Rectangle {
    id: overlay

    readonly property var colorScope: Overlay.colors

    required property var player
    // GroupSession in a room, null otherwise.
    property var group: null

    signal closeRequested()

    property var stats: ({})

    function refresh() { stats = player.stats() }

    function methodName(method) {
        return method === "DirectPlay" ? qsTr("Direct play")
            : method === "DirectStream" ? qsTr("Direct stream")
            : method === "Transcode" ? qsTr("Transcoding on the server")
            : method
    }

    function number(value, digits) {
        return Number(value).toLocaleString(Qt.locale(), "f", digits)
    }

    function megabits(bitsPerSecond) {
        return qsTr("%1 Mbit/s").arg(number(bitsPerSecond / 1e6, 1))
    }

    // [name, value] lines, the ones without a value left out.
    readonly property var lines: {
        const s = stats
        const out = []
        const add = (name, value) => { if (value !== undefined && value !== "") out.push([name, value]) }
        if (s.method !== undefined) add(qsTr("Playback"), methodName(s.method))
        // The server's own words, as its dashboard shows them.
        if (s.transcodeReasons) add(qsTr("Why"), s.transcodeReasons.split(",").join(", "))
        add(qsTr("Container"), s.container)
        if (s.videoCodec !== undefined || s.width !== undefined) {
            let video = s.videoCodec !== undefined ? s.videoCodec : ""
            if (s.width !== undefined && s.height !== undefined) video += " " + s.width + "×" + s.height
            if (s.fps !== undefined) video += " " + qsTr("%1 fps").arg(number(s.fps, 2))
            add(qsTr("Video"), video.trim())
        }
        if (s.hwdec !== undefined)
            add(qsTr("Hardware decoding"), s.hwdec === "no" ? qsTr("off") : s.hwdec)
        if (s.droppedFrames !== undefined) add(qsTr("Dropped frames"), String(s.droppedFrames))
        if (s.displayFps !== undefined) add(qsTr("Display"), qsTr("%1 Hz").arg(number(s.displayFps, 2)))
        if (s.audioCodec !== undefined) {
            let audio = s.audioCodec
            if (s.channels !== undefined) audio += ", " + qsTr("%1 ch").arg(s.channels)
            if (s.sampleRate !== undefined) audio += ", " + qsTr("%1 kHz").arg(number(s.sampleRate / 1000, 1))
            add(qsTr("Audio"), audio)
        }
        const bitrate = (s.videoBitrate || 0) + (s.audioBitrate || 0)
        if (bitrate > 0) add(qsTr("Bitrate"), megabits(bitrate))
        if (s.cacheSeconds !== undefined) add(qsTr("Buffer"), qsTr("%1 s").arg(number(s.cacheSeconds, 1)))
        if (s.cacheSpeed !== undefined) add(qsTr("Network"), megabits(s.cacheSpeed * 8))
        if (s.speed !== undefined) add(qsTr("Speed"), "×" + number(s.speed, 2))
        const g = group
        if (g && g.inGroup) {
            add(qsTr("Viewers in the room"), String(g.participants.length)
                + (g.connected ? "" : ", " + qsTr("reconnecting")))
            add(qsTr("Delay to the server"), g.pingMs >= 0 ? qsTr("%1 ms").arg(g.pingMs) : qsTr("measuring"))
            add(qsTr("Apart from the room"), g.hasDrift ? qsTr("%1 ms").arg(g.driftMs) : "–")
            add(qsTr("Catching up"), g.correction === "speed" ? qsTr("by speed")
                : g.correction === "skip" ? qsTr("by a jump") : qsTr("no"))
            add(qsTr("Room speed"), "×" + number(g.rate, 2))
        }
        return out
    }

    implicitWidth: Math.min(grid.implicitWidth + 2 * Theme.spacing + closeButton.width, 560)
    implicitHeight: grid.implicitHeight + 2 * Theme.spacing
    radius: Theme.radius
    color: "#d0000000"
    border.width: 1
    border.color: Overlay.border

    Component.onCompleted: refresh()

    Timer {
        interval: 1000
        repeat: true
        running: overlay.visible
        onTriggered: overlay.refresh()
    }

    Grid {
        id: grid
        objectName: "statsGrid"
        x: Theme.spacing
        y: Theme.spacing
        columns: 2
        columnSpacing: Theme.spacing
        rowSpacing: 2
        Repeater {
            model: overlay.lines.length * 2
            Text {
                required property int index
                readonly property var line: overlay.lines[Math.floor(index / 2)]
                objectName: index % 2 === 0 ? "statName" : "statValue"
                text: line ? line[index % 2] : ""
                color: index % 2 === 0 ? Overlay.textSecondary : Overlay.text
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                width: index % 2 === 0 ? implicitWidth : Math.min(implicitWidth, 380)
            }
        }
    }

    IconButton {
        id: closeButton
        objectName: "statsClose"
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacingTiny
        iconSource: Icons.close
        implicitWidth: 28
        implicitHeight: 28
        focusPolicy: Qt.NoFocus
        toolTipText: qsTr("Hide statistics")
        onClicked: overlay.closeRequested()
    }
}
