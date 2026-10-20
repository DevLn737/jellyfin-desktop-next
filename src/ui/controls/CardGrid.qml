import QtQuick
import Jfd.Ui.Theme

// A vertical grid of cards that fills its width (a library, search
// results). Arrow keys move between cards when focused.
FocusScope {
    id: root

    property alias model: grid.model
    property alias delegate: grid.delegate
    property alias count: grid.count
    property alias currentIndex: grid.currentIndex
    readonly property alias view: grid
    property int cardWidth: Theme.posterWidth
    property int cardHeight: Math.round(Theme.posterWidth / Theme.posterAspect) + 52

    activeFocusOnTab: true

    GridView {
        id: grid
        anchors.fill: parent
        focus: true
        clip: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        // Spread the columns evenly over the full width.
        readonly property int columns: Math.max(1, Math.floor(width / (root.cardWidth + Theme.spacing)))
        cellWidth: Math.floor(width / columns)
        cellHeight: root.cardHeight + Theme.spacingLarge
        highlightMoveDuration: Theme.durationNormal
        cacheBuffer: cellHeight * 2
    }
}
