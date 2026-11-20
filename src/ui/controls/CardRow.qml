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
    property int cardHeight: Math.round(Theme.posterWidth / Theme.posterAspect) + Theme.cardCaptionHeight
    // A "Show all" link beside the title.
    property bool canShowAll: false
    signal showAllClicked()

    // Scrolls by about a page (the arrows beside the title).
    function page(direction) {
        const target = Math.max(list.originX, Math.min(list.originX + list.contentWidth - list.width,
                                                     list.contentX + direction * list.width * 0.85))
        scroll.to = target
        scroll.restart()
    }

    implicitWidth: 800
    implicitHeight: header.implicitHeight + Theme.spacingSmall + cardHeight
    activeFocusOnTab: true

    Item {
        id: header
        objectName: "cardRowHeader"
        visible: row.title.length > 0
        width: parent.width
        implicitHeight: Math.max(title.implicitHeight, tools.height)

        SectionHeader {
            id: title
            anchors.left: parent.left
            anchors.right: tools.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            text: row.title
        }
        // For the pointer: keys move along the cards.
        Row {
            id: tools
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall
            readonly property bool scrolls: list.contentWidth > list.width + 1
            height: scrolls || row.canShowAll ? (Theme.tvMode ? 48 : 34) : 0
            visible: !Theme.tvMode

            ActionButton {
                objectName: "cardRowShowAll"
                visible: row.canShowAll
                focusPolicy: Qt.NoFocus
                height: parent.height
                leftPadding: Theme.spacing
                rightPadding: Theme.spacing
                font.pixelSize: Theme.fontSizeSmall + 1
                text: qsTr("Show all")
                onClicked: row.showAllClicked()
            }
            IconButton {
                objectName: "cardRowPrevious"
                visible: tools.scrolls
                focusPolicy: Qt.NoFocus
                square: true
                width: parent.height
                height: parent.height
                enabled: list.contentX > list.originX + 1
                iconSource: Icons.left
                toolTipText: qsTr("Scroll left")
                onClicked: row.page(-1)
            }
            IconButton {
                objectName: "cardRowNext"
                visible: tools.scrolls
                focusPolicy: Qt.NoFocus
                square: true
                width: parent.height
                height: parent.height
                enabled: list.contentX + list.width < list.originX + list.contentWidth - 1
                iconSource: Icons.right
                toolTipText: qsTr("Scroll right")
                onClicked: row.page(1)
            }
        }
    }

    NumberAnimation {
        id: scroll
        target: list
        property: "contentX"
        duration: Theme.durationSlow
        easing.type: Theme.easing
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
