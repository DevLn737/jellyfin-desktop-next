import QtQuick
import Jfd.Ui.Theme

// A titled horizontal row of cards ("Continue watching", "Latest movies").
// Scrolls with Shift + wheel, drag, and Left/Right when focused; the plain
// wheel scrolls the page. The
// delegate is usually a PosterCard bound to the model's roles.
FocusScope {
    id: row

    property string title
    property alias model: list.model
    property alias delegate: list.delegate
    property alias count: list.count
    property alias currentIndex: list.currentIndex
    readonly property alias view: list
    // Height of the tallest card; set it so the row does not resize while
    // images load.
    property int cardHeight: Math.round(Theme.posterWidth / Theme.posterAspect) + 52

    implicitWidth: 800
    implicitHeight: header.implicitHeight + Theme.spacingSmall + cardHeight
    activeFocusOnTab: true

    SectionHeader {
        id: header
        text: row.title
        visible: row.title.length > 0
    }

    ListView {
        id: list
        anchors.top: header.visible ? header.bottom : parent.top
        anchors.topMargin: header.visible ? Theme.spacingSmall : 0
        width: parent.width
        height: row.cardHeight
        orientation: ListView.Horizontal
        spacing: Theme.spacing
        focus: true
        clip: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: Theme.durationNormal
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: Theme.spacingHuge
        preferredHighlightEnd: width - Theme.spacingHuge
        cacheBuffer: 2 * Theme.thumbWidth

        // A plain vertical wheel belongs to the page (it passes through the
        // row); Shift + wheel scrolls the row sideways. A sideways wheel or
        // touchpad swipe is the list's own.
        WheelHandler {
            orientation: Qt.Vertical
            acceptedModifiers: Qt.ShiftModifier
            target: null
            onWheel: (event) => list.flick(event.angleDelta.y * 8, 0)
        }
    }
}
