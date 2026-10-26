import QtQuick
import Jfd.Ui.Theme

// Shown where music would play while the viewer is in a SyncPlay group,
// which has no music.
Text {
    visible: !MusicSession.available
    text: qsTr("Leave the group to play music")
    color: Theme.textSecondary
    font.pixelSize: Theme.fontSizeSmall
    wrapMode: Text.Wrap
}
