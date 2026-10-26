pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// An album, artist, playlist or genre in a grid or row. Clicking opens its
// page; the context menu plays it, shuffles it or queues it.
PosterCard {
    id: card

    // The delegate's model roles (MusicListModel) and the model itself.
    required property var model
    required property int index
    property var listModel
    property PlayGuard guard

    readonly property bool playable: model.type === "MusicAlbum" || model.type === "MusicArtist" || model.type === "Playlist"

    shape: model.type === "MusicArtist" ? "circle" : "square"
    title: model.name || ""
    subtitle: model.subtitle || ""
    imageSource: model.loaded === false ? "" : Library.image(model.imageUrl)

    onClicked: {
        const route = listModel ? listModel.routeFor(index) : ""
        if (route.length > 0)
            Navigator.navigate(route)
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: card.playable
        onTapped: menu.popup()
    }
    Keys.onMenuPressed: if (card.playable) menu.popup()

    ActionMenu {
        id: menu
        function popup() {
            x = Math.min(card.width, 40)
            y = Math.min(card.height, 40)
            open()
        }
        parent: card
        entries: card.model.type === "MusicAlbum"
            ? [qsTr("Play"), qsTr("Shuffle"), qsTr("Play next"), qsTr("Add to queue")]
            : [qsTr("Play"), qsTr("Shuffle")]
        disabledIndices: MusicSession.available ? [] : [0, 1, 2, 3]
        onActivated: (i) => {
            const run = (action) => card.guard ? card.guard.run(action) : action()
            if (i === 0) run(() => card.listModel.play(card.index, false))
            else if (i === 1) run(() => card.listModel.play(card.index, true))
            else if (i === 2) card.listModel.enqueue(card.index, true)
            else if (i === 3) card.listModel.enqueue(card.index, false)
        }
    }
}
