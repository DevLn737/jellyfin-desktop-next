import QtQuick
import JellyfinDesktop.Media

Window {
    id: root

    required property MpvPlayer player
    required property var app

    width: 1280
    height: 720
    visible: true
    color: "black"
    title: player.title.length > 0 ? player.title + " — Jellyfin Desktop Next" : "Jellyfin Desktop Next"

    property bool menuOpen: app.animateMenu
    property bool osdVisible: true

    MpvVideo {
        id: video
        objectName: "video"
        anchors.fill: parent
        player: root.player
    }

    // Hides the on-screen display a few seconds after the last mouse move.
    Timer {
        id: osdTimer
        interval: 3000
        running: !root.app.animateMenu
        onTriggered: root.osdVisible = root.menuOpen
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.osdVisible ? Qt.ArrowCursor : Qt.BlankCursor
        onPositionChanged: { root.osdVisible = true; osdTimer.restart() }
        onDoubleClicked: root.app.toggleFullscreen()
        onClicked: (mouse) => { if (root.menuOpen && !root.app.animateMenu) root.menuOpen = false }
    }

    OsdBar {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        player: root.player
        shown: root.osdVisible || root.app.animateMenu
        onMenuRequested: root.menuOpen = !root.menuOpen
    }

    PlayerMenu {
        id: menu
        video: video
        open: root.menuOpen
        animate: root.app.animateMenu
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.topMargin: 24
        anchors.bottomMargin: 120
    }

    Text {
        visible: root.app.statsVisible
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 16
        color: "white"
        style: Text.Outline
        styleColor: "black"
        font.pixelSize: 15
        font.family: "monospace"
        text: root.app.statsText
    }

    Item {
        focus: true
        Keys.onPressed: (event) => {
            switch (event.key) {
            case Qt.Key_Space: root.player.togglePause(); break
            case Qt.Key_F:
            case Qt.Key_Return: root.app.toggleFullscreen(); break
            case Qt.Key_Escape:
                if (root.menuOpen) root.menuOpen = false
                else if (root.visibility === Window.FullScreen) root.app.toggleFullscreen()
                break
            case Qt.Key_M: root.menuOpen = !root.menuOpen; break
            case Qt.Key_I: root.app.statsVisible = !root.app.statsVisible; break
            case Qt.Key_Left: root.player.seekRelative(-10); break
            case Qt.Key_Right: root.player.seekRelative(10); break
            case Qt.Key_Q: root.app.quit(); break
            default: return
            }
            root.osdVisible = true
            osdTimer.restart()
            event.accepted = true
        }
    }
}
