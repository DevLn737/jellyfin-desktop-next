pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// A card for one row of an item list (home rows, a library grid, search,
// similar items). Clicking opens the item's page, or the library for a
// library or collection; an episode plays, and so do a track (in the
// music player, its album as the queue) and a Live TV channel ("On now",
// on this device only). An episode shows its series as the title and
// "S1:E2 · Name" below, as Jellyfin Web does.
//
// Films, series and episodes have quick actions on the image (Play,
// Watched, Favorite) and a menu by right click or the Menu key: Play, Play
// from the start, Watched, Favorite, Go to series. Not offline: there is
// no server to tell.
PosterCard {
    id: card

    // The delegate's model roles (ItemListModel).
    required property var model
    required property int index

    // The offline home's cards: a film plays too (there is no item page
    // without the server).
    property bool playOnClick: false

    readonly property string itemId: model.itemId || ""
    readonly property bool isEpisode: model.type === "Episode" && (model.seriesName || "").length > 0
    readonly property var videoTypes: ["Movie", "Episode", "Video", "MusicVideo"]
    readonly property bool isVideo: videoTypes.indexOf(model.type) >= 0
    readonly property bool resumable: (model.playbackPositionTicks || 0) > 0

    function play(fromStart) {
        if (model.type === "Series")
            Navigator.navigate(Library.routeFor(itemId, model.type))
        else
            Library.play(itemId, fromStart ? 0 : (model.playbackPositionTicks || 0))
    }

    title: isEpisode ? model.seriesName : (model.name || "")
    subtitle: isEpisode
        ? ((model.subtitle || "").length > 0 ? model.subtitle + " · " + model.name : model.name)
        : (model.subtitle || "")
    // A downloaded item's own image is a file; others come from the server.
    imageSource: String(model.imageUrl || "").startsWith("file:") ? model.imageUrl : Library.image(model.imageUrl)
    played: model.played === true
    progress: model.progress || 0
    unplayedCount: model.unplayedCount || 0
    favorite: model.favorite === true
    quickActions: !card.playOnClick && Library.online && itemId.length > 0
                  && (isVideo || model.type === "Series")
    canPlay: isVideo
    onPlayRequested: card.play(false)
    onPlayedToggled: Library.setPlayed(itemId, !card.played, card.favorite)
    onFavoriteToggled: Library.setFavorite(itemId, !card.favorite)
    onMenuRequested: (at) => {
        menuAnchor.x = at.x
        menuAnchor.y = at.y
        // Made on first use: a grid has hundreds of cards.
        menuLoader.active = true
        menuLoader.item.open()
    }
    // Downloaded, or downloading (the series or album for its items).
    downloadState: Downloads.revision >= 0 && itemId.length > 0 ? Downloads.stateOf(itemId) : ""
    downloadProgress: downloadState === "downloading" || downloadState === "paused"
        ? (Downloads.progressRevision >= 0 ? Downloads.progressOf(itemId) : 0) : 0

    onClicked: {
        if (itemId.length === 0)
            return
        // An episode has no page of its own: it plays (resuming where it
        // stopped). Its series page lists the others.
        if (model.type === "Episode" || (card.playOnClick && model.type !== "Audio"))
            Library.play(itemId, model.playbackPositionTicks || 0)
        else if (model.type === "Audio" && (card.playOnClick || !Library.online) && Downloads.stateOf(itemId) === "complete")
            Downloads.playTrack(itemId)
        else if (model.type === "Audio")
            Library.playTrack(itemId, model.albumId || "")
        // A Live TV channel ("On now") tunes in, on this device only.
        else if (model.type === "TvChannel")
            Library.playChannel(itemId)
        else
            Navigator.navigate(Library.routeFor(itemId, model.type))
    }

    Item { id: menuAnchor; width: 1; height: 1 }

    Loader {
        id: menuLoader
        active: false
        sourceComponent: cardMenu
    }

    Component {
        id: cardMenu
        PopupMenu {
            id: menu
            objectName: "cardMenu"
            anchorItem: menuAnchor
            // [text, action] pairs, only those that apply.
            readonly property var actions: {
                const out = []
                if (card.isVideo) {
                    out.push([card.resumable ? qsTr("Resume") : qsTr("Play"), () => card.play(false)])
                    if (card.resumable)
                        out.push([qsTr("Play from the start"), () => card.play(true)])
                } else {
                    out.push([qsTr("Open"), () => card.clicked()])
                }
                out.push([card.played ? qsTr("Mark as not watched") : qsTr("Mark as watched"),
                          () => Library.setPlayed(card.itemId, !card.played, card.favorite)])
                out.push([card.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites"),
                          () => Library.setFavorite(card.itemId, !card.favorite)])
                if (card.isEpisode && (card.model.seriesId || "").length > 0)
                    out.push([qsTr("Go to series"), () => Navigator.navigate(Library.routeFor(card.model.seriesId, "Series"))])
                return out
            }
            entries: actions.map((a) => a[0])
            onActivated: (i) => {
                const action = actions[i]
                if (action)
                    action[1]()
            }
        }
    }
}
