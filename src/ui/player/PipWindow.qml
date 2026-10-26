pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The picture-in-picture window: a small frameless window that stays above
// the others and shows the video while the main window is free. Always
// present (the application creates it once); shown while PlayerSession.pip.
// Controls show on hover: play/pause and back to the player page. Esc, the
// back button and closing the window all go back to the player page; the
// video never stops.
//
// Where the desktop gives applications no way to stay on top or to choose a
// position (Wayland), the window opens where the compositor puts it, a hint
// says so, and the title is fixed so users can write a window rule.
Window {
    id: pip

    readonly property var player: PlayerSession
    readonly property bool wayland: Qt.platform.pluginName.indexOf("wayland") === 0
    // Where the hint about keeping the window on top shows.
    property bool hintShown: false

    // Not translated: a fixed title is what a compositor rule matches.
    readonly property string fixedTitle: "Jellyfin Desktop Next - Picture in picture"
    title: fixedTitle
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    width: 480
    height: 270
    minimumWidth: 240
    minimumHeight: 135
    color: "black"
    visible: player.pip

    onVisibleChanged: {
        if (!visible) return
        if (!wayland) {
            x = Screen.virtualX + Screen.desktopAvailableWidth - width - Theme.spacingLarge
            y = Screen.virtualY + Screen.desktopAvailableHeight - height - Theme.spacingLarge
        }
        hintShown = wayland
        hintTimer.restart()
        // The host belongs to this window; the video item goes away with it.
        player.attachVideo(videoHost, null)
        content.forceActiveFocus()
    }

    // Closing the window (Alt+F4, the window manager) is "back to the player".
    onClosing: (close) => {
        close.accepted = false
        if (player.pip) player.leavePip()
    }

    Connections {
        target: pip.player
        // The file ended with nothing after it.
        function onStateChanged() {
            if (pip.player.pip && pip.player.ended && !pip.player.inGroup && !pip.player.hasNext)
                pip.player.stop()
        }
    }

    Item {
        id: videoHost
        anchors.fill: parent
    }

    FocusScope {
        id: content
        anchors.fill: parent
        focus: true

        Keys.onPressed: (event) => {
            if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) {
                event.accepted = false
                return
            }
            switch (event.key) {
            case Qt.Key_Space:
            case Qt.Key_K:
                pip.player.togglePause(); break
            case Qt.Key_Escape:
                pip.player.leavePip(); break
            default:
                event.accepted = false
                return
            }
            event.accepted = true
        }

        // Drag the window anywhere on the video; a click pauses.
        MouseArea {
            id: area
            objectName: "pipArea"
            anchors.fill: parent
            hoverEnabled: true
            onClicked: pip.player.togglePause()
        }
        DragHandler {
            target: null
            onActiveChanged: if (active) pip.startSystemMove()
        }

        Rectangle {
            id: bar
            objectName: "pipBar"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Theme.controlHeight + Theme.spacing
            color: Qt.rgba(0, 0, 0, 0.6)
            opacity: area.containsMouse || barHover.hovered || pip.player.paused ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: 150 } }
            HoverHandler { id: barHover }

            Row {
                anchors.centerIn: parent
                spacing: Theme.spacing
                IconButton {
                    objectName: "pipPlayPause"
                    iconSource: pip.player.paused ? Icons.play : PlayerIcons.pause
                    toolTipText: pip.player.paused ? qsTr("Play") : qsTr("Pause")
                    onClicked: pip.player.togglePause()
                }
                IconButton {
                    objectName: "pipBack"
                    iconSource: PlayerIcons.fullscreen
                    toolTipText: qsTr("Back to the player")
                    onClicked: pip.player.leavePip()
                }
            }
        }

        // Resize from the corner.
        MouseArea {
            objectName: "pipResize"
            width: 20
            height: 20
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            cursorShape: Qt.SizeFDiagCursor
            onPressed: pip.startSystemResize(Qt.RightEdge | Qt.BottomEdge)
        }

        Text {
            objectName: "pipHint"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            padding: Theme.spacing
            visible: pip.hintShown
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: Theme.fontSizeSmall
            text: qsTr("Your desktop does not let applications keep a window on top. Use the window menu's Always on top.")
            Rectangle {
                anchors.fill: parent
                z: -1
                color: Qt.rgba(0, 0, 0, 0.7)
            }
        }
    }

    Timer {
        id: hintTimer
        interval: 8000
        onTriggered: pip.hintShown = false
    }
}
