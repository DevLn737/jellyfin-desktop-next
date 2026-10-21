pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// A card for one row of an item list (home rows, a library grid, search,
// similar items). Clicking opens the item's page, or the library for a
// library or collection. An episode shows its series as the title and
// "S1:E2 · Name" below, as Jellyfin Web does.
PosterCard {
    id: card

    // The delegate's model roles (ItemListModel).
    required property var model
    required property int index

    readonly property string itemId: model.itemId || ""
    readonly property bool isEpisode: model.type === "Episode" && (model.seriesName || "").length > 0

    title: isEpisode ? model.seriesName : (model.name || "")
    subtitle: isEpisode
        ? ((model.subtitle || "").length > 0 ? model.subtitle + " · " + model.name : model.name)
        : (model.subtitle || "")
    imageSource: Library.image(model.imageUrl)
    played: model.played === true
    progress: model.progress || 0
    unplayedCount: model.unplayedCount || 0

    onClicked: {
        if (itemId.length > 0)
            Navigator.navigate(Library.routeFor(itemId, model.type))
    }
}
