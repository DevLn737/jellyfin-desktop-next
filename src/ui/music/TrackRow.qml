pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// A track in an album, a playlist or the song list: number (or the speaker
// of the track playing now), title and artist, album, length. Clicking
// plays it with the tracks after it; the menu queues it or goes to its
// album and artist.
T.ItemDelegate {
    id: row

    // The delegate's model roles (MusicListModel) and the model itself.
    required property var model
    required property int index
    property var listModel
    property PlayGuard guard
    property bool showArtist: true
    property bool showAlbum: false
    // Number the rows by their position instead of the track number
    // (playlists, the song list).
    property bool numbered: false
    property bool removable: false

    signal removeRequested()

    readonly property bool current: (MusicSession.currentTrack.id || "") === (model.itemId || "") && (model.itemId || "").length > 0

    implicitHeight: Theme.controlHeight + Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    leftPadding: Theme.spacing
    rightPadding: Theme.spacingSmall
    Accessible.name: model.name || ""

    Keys.onReturnPressed: row.clicked()
    Keys.onEnterPressed: row.clicked()
    Keys.onMenuPressed: menu.popup()
    onClicked: if (guard) guard.run(() => listModel.play(index, false))

    background: Rectangle {
        radius: Theme.radius
        color: row.hovered || row.visualFocus ? Theme.surfaceHover : "transparent"
        FocusFrame { shown: row.visualFocus }
    }

    contentItem: Item {
        Text {
            id: number
            width: 36
            anchors.verticalCenter: parent.verticalCenter
            visible: !row.current
            text: row.numbered ? row.index + 1 : (row.model.indexNumber || "")
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            horizontalAlignment: Text.AlignRight
            rightPadding: Theme.spacing
        }
        Icon {
            width: 36
            anchors.verticalCenter: parent.verticalCenter
            visible: row.current
            source: MusicSession.playing ? Icons.play : MusicIcons.pause
            size: 18
        }

        Column {
            anchors.left: number.right
            anchors.right: album.visible ? album.left : length.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: row.model.name || ""
                color: row.current ? Theme.accent : Theme.text
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: row.showArtist
                text: row.model.artistName || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
        }

        Text {
            id: album
            width: parent.width * 0.25
            anchors.right: length.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            visible: row.showAlbum && parent.width > 700
            text: row.model.albumName || ""
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            elide: Text.ElideRight
        }

        Text {
            id: length
            anchors.right: more.left
            anchors.rightMargin: Theme.spacingSmall
            anchors.verticalCenter: parent.verticalCenter
            text: Music.formatDuration(row.model.duration || 0)
            color: Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
        }

        IconButton {
            id: more
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconSource: MusicIcons.more
            toolTipText: qsTr("More")
            onClicked: menu.popup()
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: menu.popup()
    }

    ActionMenu {
        id: menu
        function popup() {
            x = Math.max(0, row.width - width - Theme.spacing)
            y = row.height
            open()
        }
        parent: row
        entries: {
            const list = [qsTr("Play next"), qsTr("Add to queue")]
            if ((row.model.albumId || "").length > 0) list.push(qsTr("Go to album"))
            if ((row.model.artistId || "").length > 0) list.push(qsTr("Go to artist"))
            if (row.removable) list.push(qsTr("Remove from playlist"))
            return list
        }
        onActivated: (i) => {
            const label = entries[i]
            if (label === qsTr("Play next")) row.listModel.enqueue(row.index, true)
            else if (label === qsTr("Add to queue")) row.listModel.enqueue(row.index, false)
            else if (label === qsTr("Go to album")) Navigator.navigate("album/" + row.model.albumId)
            else if (label === qsTr("Go to artist")) Navigator.navigate("artist/" + row.model.artistId)
            else row.removeRequested()
        }
    }
}
