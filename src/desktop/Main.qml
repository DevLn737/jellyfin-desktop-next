import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Player

// The main window. The application places and shows it (WindowStateKeeper).
Window {
    id: window
    objectName: "mainWindow"

    width: 1280
    height: 800
    minimumWidth: 640
    minimumHeight: 480
    visible: false
    // With HDR10 output the clear colour reaches the display unencoded.
    color: scene.hdr ? "black" : Theme.background
    title: shell.title.length > 0 ? shell.title + " — Jellyfin" : "Jellyfin"

    // The interface layer, encoded to PQ with HDR10 output; the player's
    // video goes underneath.
    HdrScene {
        id: scene
        anchors.fill: parent

        AppShell {
            id: shell
            objectName: "shell"
            anchors.fill: parent
            backgroundVisible: !HdrOutput.videoShown
        }
    }
}
