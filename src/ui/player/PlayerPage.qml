pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Group
import Jfd.Ui.Player

// The player: video under an on-screen display that hides after a few
// seconds without the mouse. Top: back, title and the group indicator.
// Bottom: the timeline, play/pause, jumps, next, track and playback menus,
// fullscreen. A skip button shows during intros and credits.
RoutePage {
    id: page

    readonly property var player: PlayerSession
    readonly property bool fullscreen: Window.window !== null && Window.window.visibility === Window.FullScreen
    property bool osdVisible: true
    // "audio", "subtitles", "tune" or "".
    property string openMenu: ""
    readonly property bool osdShown: osdVisible || openMenu.length > 0 || player.paused || !player.active

    title: player.title
    focus: true

    function clock(seconds) {
        const total = Math.max(0, Math.floor(seconds))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const pad = (n) => n < 10 ? "0" + n : "" + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    function poke() {
        osdVisible = true
        osdTimer.restart()
    }

    function toggleFullscreen() {
        const window = Window.window
        if (!window) return
        window.visibility = fullscreen ? Window.Windowed : Window.FullScreen
    }

    function toggleMenu(name) {
        openMenu = openMenu === name ? "" : name
        poke()
    }

    function leave() {
        if (fullscreen) toggleFullscreen()
        Navigator.back()
    }

    // With HDR10 output the video goes under the window's interface layer
    // (HdrScene); the page stays transparent over it.
    readonly property bool videoUnder: HdrOutput.active && HdrOutput.videoLayer !== null

    Component.onCompleted: player.attachVideo(videoUnder ? HdrOutput.videoLayer : videoHost, page)
    Component.onDestruction: player.stop()

    Connections {
        target: page.player
        // Back to where playback started once the file ends on its own.
        function onStateChanged() {
            if (page.player.ended && !page.player.inGroup && !page.player.hasNext) page.leave()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: !page.videoUnder
    }

    Item {
        id: videoHost
        anchors.fill: parent
    }

    Timer {
        id: osdTimer
        interval: 3000
        running: true
        onTriggered: page.osdVisible = false
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: page.osdShown ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: page.poke()
        onClicked: {
            if (page.openMenu.length > 0) page.openMenu = ""
            else page.player.togglePause()
            page.poke()
        }
        onDoubleClicked: page.toggleFullscreen()
    }

    LoadingIndicator {
        anchors.centerIn: parent
        visible: page.player.loading
    }

    Text {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacingHuge, 640)
        visible: page.player.error.length > 0
        text: qsTr("Playback failed: %1").arg(page.player.error)
        color: Theme.text
        font.pixelSize: Theme.fontSizeLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    // Top bar.
    Rectangle {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Theme.controlHeight + 2 * Theme.spacing
        opacity: page.osdShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        gradient: Gradient {
            GradientStop { position: 0; color: "#c0000000" }
            GradientStop { position: 1; color: "transparent" }
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            spacing: Theme.spacing

            IconButton {
                iconSource: Icons.back
                toolTipText: qsTr("Back")
                onClicked: page.leave()
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - 2 * Theme.controlHeight - groupOsd.width - 3 * Theme.spacing
                text: page.player.title
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                elide: Text.ElideRight
            }
            // Only in a group: GroupSession exists once the group module
            // is installed.
            Loader {
                id: groupOsd
                anchors.verticalCenter: parent.verticalCenter
                active: page.player.inGroup
                sourceComponent: GroupOsd {}
            }
        }
    }

    ActionButton {
        objectName: "skipSegment"
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.margins: Theme.spacingLarge
        visible: page.player.segment.length > 0
        primary: true
        text: page.player.segment === "Intro" ? qsTr("Skip intro")
            : page.player.segment === "Outro" ? qsTr("Skip credits")
            : page.player.segment === "Recap" ? qsTr("Skip recap")
            : page.player.segment === "Preview" ? qsTr("Skip preview")
            : qsTr("Skip")
        onClicked: page.player.skipSegment()
    }

    // Menus open above the bottom bar, on the right.
    Loader {
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.margins: Theme.spacing
        active: page.openMenu.length > 0
        sourceComponent: page.openMenu === "tune" ? tuneMenu : trackMenu
    }
    Component {
        id: trackMenu
        TrackMenu {
            readonly property bool audio: page.openMenu === "audio"
            heading: audio ? qsTr("Audio") : qsTr("Subtitles")
            tracks: audio ? page.player.audioTracks : page.player.subtitleTracks
            currentIndex: audio ? page.player.audioIndex : page.player.subtitleIndex
            allowOff: !audio
            onChosen: (streamIndex) => {
                if (audio) page.player.selectAudio(streamIndex)
                else page.player.selectSubtitle(streamIndex)
                page.openMenu = ""
            }
        }
    }
    Component {
        id: tuneMenu
        TuneMenu {}
    }

    // Bottom bar.
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: column.implicitHeight + 2 * Theme.spacing
        opacity: page.osdShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: "#d0000000" }
        }

        Column {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing
            spacing: Theme.spacingTiny

            ValueSlider {
                id: timeline
                objectName: "timeline"
                width: parent.width
                from: 0
                to: Math.max(page.player.duration, 1)
                stepSize: 10
                // Follows playback except while dragged.
                Binding on value {
                    when: !timeline.pressed
                    value: page.player.position
                }
                onMoved: page.poke()
                onPressedChanged: if (!pressed) page.player.seek(value)
            }

            Item {
                width: parent.width
                height: Theme.controlHeight

                Row {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSmall

                    IconButton {
                        objectName: "playPause"
                        iconSource: page.player.paused ? Icons.play : PlayerIcons.pause
                        toolTipText: page.player.paused ? qsTr("Play") : qsTr("Pause")
                        onClicked: page.player.togglePause()
                    }
                    IconButton {
                        iconSource: PlayerIcons.replay
                        toolTipText: qsTr("Back 10 seconds")
                        onClicked: page.player.seekBy(-10)
                    }
                    IconButton {
                        iconSource: PlayerIcons.forward
                        toolTipText: qsTr("Forward 30 seconds")
                        onClicked: page.player.seekBy(30)
                    }
                    IconButton {
                        objectName: "next"
                        // A group's queue is the server's; alone, the series'.
                        visible: page.player.inGroup || page.player.hasNext
                        iconSource: PlayerIcons.next
                        toolTipText: qsTr("Next")
                        onClicked: page.player.next()
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.clock(timeline.value) + " / " + page.clock(page.player.duration)
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                    }
                }

                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSmall

                    IconButton {
                        iconSource: PlayerIcons.audio
                        toolTipText: qsTr("Audio")
                        onClicked: page.toggleMenu("audio")
                    }
                    IconButton {
                        iconSource: PlayerIcons.subtitles
                        toolTipText: qsTr("Subtitles")
                        onClicked: page.toggleMenu("subtitles")
                    }
                    IconButton {
                        iconSource: PlayerIcons.tune
                        toolTipText: qsTr("Playback settings")
                        onClicked: page.toggleMenu("tune")
                    }
                    IconButton {
                        iconSource: page.fullscreen ? PlayerIcons.fullscreenExit : PlayerIcons.fullscreen
                        toolTipText: page.fullscreen ? qsTr("Exit fullscreen") : qsTr("Fullscreen")
                        onClicked: page.toggleFullscreen()
                    }
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        switch (event.key) {
        case Qt.Key_Space:
        case Qt.Key_K:
            page.player.togglePause(); break
        case Qt.Key_Left:
        case Qt.Key_J:
            page.player.seekBy(-10); break
        case Qt.Key_Right:
        case Qt.Key_L:
            page.player.seekBy(30); break
        case Qt.Key_F:
            page.toggleFullscreen(); break
        case Qt.Key_S:
            page.player.skipSegment(); break
        case Qt.Key_N:
            page.player.next(); break
        case Qt.Key_A:
            page.toggleMenu("audio"); break
        case Qt.Key_C:
            page.toggleMenu("subtitles"); break
        case Qt.Key_Z:
            page.player.subtitleDelay = page.player.subtitleDelay - 0.1; break
        case Qt.Key_X:
            page.player.subtitleDelay = page.player.subtitleDelay + 0.1; break
        case Qt.Key_Escape:
        case Qt.Key_Back:
            if (page.openMenu.length > 0) page.openMenu = ""
            else if (page.fullscreen) page.toggleFullscreen()
            else page.leave()
            break
        default:
            return
        }
        page.poke()
        event.accepted = true
    }
}
