import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A menu that opens beside a button: under it with an 8 px gap, above it if
// there is no room, and always inside the window. Set `anchorItem` to the
// button and call open(). `entries` are the texts, `currentIndex` the one
// marked, `activated(index)` says which was chosen. Esc, a click outside or
// choosing closes it; Up/Down/Enter work from the keyboard. One light
// shadow, drawn with plain rectangles (no blur, no extra window).
T.Popup {
    id: menu

    property Item anchorItem
    property var entries: []
    property var disabledIndices: []
    property int currentIndex: -1
    // Line the menu's right edge up with the button's instead of the left.
    property bool alignRight: false
    readonly property int gap: 8

    signal activated(int index)

    function reposition() {
        const host = menu.parent
        if (!menu.anchorItem || !host)
            return
        const corner = menu.anchorItem.mapToItem(host, 0, 0)
        const margin = Theme.spacingSmall
        const wantX = menu.alignRight ? corner.x + menu.anchorItem.width - menu.width : corner.x
        const below = corner.y + menu.anchorItem.height + menu.gap
        const above = corner.y - menu.gap - menu.height
        const fitsBelow = below + menu.height <= host.height - margin
        let wantY = fitsBelow || above < margin ? below : above
        wantY = Math.max(margin, Math.min(wantY, host.height - menu.height - margin))
        menu.x = Math.max(margin, Math.min(wantX, host.width - menu.width - margin))
        menu.y = wantY
    }

    parent: T.Overlay.overlay
    padding: Theme.spacingTiny
    margins: Theme.spacingSmall
    width: 240
    // A templated Popup has no height of its own: the entries' plus padding.
    implicitHeight: contentHeight + topPadding + bottomPadding
    modal: false
    focus: true
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    // The overlay (the window) can be resized or only get its size once the
    // menu is open: follow it.
    Connections {
        target: menu.parent
        function onWidthChanged() { if (menu.visible) menu.reposition() }
        function onHeightChanged() { if (menu.visible) menu.reposition() }
    }

    onAboutToShow: reposition()
    onHeightChanged: if (visible) reposition()
    onWidthChanged: if (visible) reposition()

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFast }
    }

    background: Item {
        // The shadow: a few widening, fainter outlines under the panel.
        Repeater {
            model: 3
            Rectangle {
                required property int index
                x: -(index + 1) * 3
                y: -(index + 1) * 3 + Theme.shadowOffset
                width: parent.width + (index + 1) * 6
                height: parent.height + (index + 1) * 6
                radius: Theme.radiusLarge + (index + 1) * 3
                color: Qt.alpha(Theme.shadowColor, 0.18 - index * 0.05)
            }
        }
        GlassPanel {
            anchors.fill: parent
            popup: true
        }
    }

    // A column, not a list: menus are short, and the height is known as
    // soon as the entries are.
    contentItem: Column {
        id: list
        Repeater {
            model: menu.entries
            delegate: ListItem {
                id: entry
                objectName: "popupEntry"
                required property string modelData
                required property int index
                width: list.width
                text: modelData
                trailingText: index === menu.currentIndex ? "✓" : ""
                enabled: menu.disabledIndices.indexOf(index) < 0
                onClicked: {
                    menu.close()
                    menu.activated(index)
                }
            }
        }
    }
}
