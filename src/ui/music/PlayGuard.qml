import QtQuick
import Jfd.Ui.Controls

// Guards starting music. With a video playing, the music would replace it,
// so the guard asks first; in a SyncPlay group music is not available and
// nothing happens. `run(action)` calls the action when it may.
Item {
    id: guard

    property var pending: null

    function run(action) {
        if (!MusicSession.available)
            return
        if (MusicSession.videoActive) {
            guard.pending = action
            dialog.open()
        } else {
            action()
        }
    }

    AppDialog {
        id: dialog
        title: qsTr("Stop the video?")
        text: qsTr("Playing music stops the video that is playing now.")
        acceptText: qsTr("Play music")
        onAccepted: {
            const action = guard.pending
            guard.pending = null
            if (action)
                action()
        }
        onRejected: guard.pending = null
    }
}
