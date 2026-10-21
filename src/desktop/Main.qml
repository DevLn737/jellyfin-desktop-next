import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Shell
import Jfd.Ui.Settings

// The main window. The application places and shows it (WindowStateKeeper).
Window {
    id: window
    objectName: "mainWindow"

    width: 1280
    height: 800
    minimumWidth: 640
    minimumHeight: 480
    visible: false
    color: Theme.background
    title: shell.title.length > 0 ? shell.title + " — Jellyfin" : "Jellyfin"

    AppShell {
        id: shell
        objectName: "shell"
        anchors.fill: parent
    }

    Binding {
        target: Theme
        property: "reduceMotion"
        value: AppSettings.revision >= 0 && AppSettings.value("interface/reduceMotion") === true
    }
}
