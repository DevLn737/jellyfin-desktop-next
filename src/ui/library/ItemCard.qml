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

    title: isEpisode ? model.seriesName : (model.name || "")
    subtitle: isEpisode
        ? ((model.subtitle || "").length > 0 ? model.subtitle + " · " + model.name : model.name)
        : (model.subtitle || "")
    // A downloaded item's own image is a file; others come from the server.
    imageSource: String(model.imageUrl || "").startsWith("file:") ? model.imageUrl : Library.image(model.imageUrl)
    played: model.played === true
    progress: model.progress || 0
    unplayedCount: model.unplayedCount || 0
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
}
