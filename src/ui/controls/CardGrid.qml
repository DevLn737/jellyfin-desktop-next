import QtQuick
import Jfd.Ui.Theme

// A vertical grid of cards that fills its width (a library, search
// results). Arrow keys move between cards when focused. In TV mode the
// arrows leave the grid at its edges (Left from the first column goes to
// the sidebar) instead of wrapping to the row above or below, and Down
// from a row over a shorter last row goes to its last card.
FocusScope {
    id: root

    property alias model: grid.model
    property alias delegate: grid.delegate
    property alias count: grid.count
    property alias currentIndex: grid.currentIndex
    readonly property alias view: grid
    property int cardWidth: Theme.posterWidth
    property int cardHeight: Math.round(Theme.posterWidth / Theme.posterAspect) + Theme.cardCaptionHeight

    activeFocusOnTab: true

    GridView {
        SmoothWheel {}
        id: grid
        anchors.fill: parent
        focus: true
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        // Spread the columns evenly over the full width.
        readonly property int columns: Math.max(1, Math.floor(width / (root.cardWidth + Theme.spacing)))
        cellWidth: Math.floor(width / columns)
        cellHeight: root.cardHeight + Theme.spacingLarge
        highlightMoveDuration: Theme.durationNormal
        cacheBuffer: cellHeight * 2
        keyNavigationEnabled: !Theme.tvMode

        // TV mode: the grid's own moves; a key it cannot use goes on to the
        // page and the shell's SpatialNavigation.
        function step(target, event) {
            if (!Theme.tvMode || target < 0 || target >= count || target === currentIndex) {
                event.accepted = false
                return
            }
            currentIndex = target
            positionViewAtIndex(target, GridView.Contain)
            event.accepted = true
        }
        Keys.onLeftPressed: (event) => step(currentIndex % columns > 0 ? currentIndex - 1 : -1, event)
        Keys.onRightPressed: (event) => step(currentIndex % columns < columns - 1 ? currentIndex + 1 : -1, event)
        Keys.onUpPressed: (event) => step(currentIndex - columns, event)
        Keys.onDownPressed: (event) => {
            const lastRow = Math.floor((count - 1) / columns)
            const row = Math.floor(currentIndex / columns)
            step(row < lastRow ? Math.min(currentIndex + columns, count - 1) : -1, event)
        }
    }
}
