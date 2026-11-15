import QtQuick
import QtQuick.Dialogs

// The system's folder picker for a "folder" setting (SettingRow loads it on
// first use). `chosen` gives the folder as a file URL.
FolderDialog {
    id: dialog

    signal chosen(url folder)

    title: qsTr("Choose a folder")
    onAccepted: chosen(dialog.selectedFolder)
}
