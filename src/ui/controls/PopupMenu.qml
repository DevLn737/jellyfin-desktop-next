import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A menu that opens beside a button: under it with an 8 px gap, above it if
// there is no room, and always inside the window. Set `anchorItem` to the
// button and call open(). `entries` are the texts, `currentIndex` the one
// marked, `activated(index)` says which was chosen. Esc, a click outside or
// choosing closes it. It opens with the focus on the marked entry (else the
// first), so Up/Down (wrapping round), Home/End and Enter work at once from
// the keyboard and a gamepad (InputRouter sends its d-pad as arrows, A as
// Enter, B as Escape). The entry under the pointer takes the focus, so one
// entry at a time is lit: by the pointer, or by keys once a key moved it or
// the menu was opened from the keyboard. Thin lines part the entries. When
// it closes, the focus goes back where it was (or to `returnFocusTo`)
// unless something else took it. One light shadow, drawn with plain
// rectangles (no blur, no extra window).
T.Popup {
    id: menu

    property Item anchorItem
    property var entries: []
    property var disabledIndices: []
    property int currentIndex: -1
    // Line the menu's right edge up with the button's instead of the left.
    property bool alignRight: false
    readonly property int gap: 8
    // Gets the focus back when the menu closes, whatever has it then.
    property Item returnFocusTo
    // The focused entry is lit: opened from the keyboard or a gamepad, or a
    // key pressed in it since.
    property bool keyMode: false
    // The item that had the focus when the menu opened.
    property Item previousFocus

    signal activated(int index)

    function entryAt(index) { return entryRepeater.itemAt(index) }
    // The next enabled entry from `from` by `step` (1 or -1), wrapping round.
    function nextEnabled(from, step) {
        const count = entryRepeater.count
        for (let i = 1; i <= count; ++i) {
            const index = ((from + step * i) % count + count) % count
            const entry = entryRepeater.itemAt(index)
            if (entry && entry.enabled)
                return index
        }
        return -1
    }
    function focusEntry(index, reason) {
        const entry = entryRepeater.itemAt(index)
        if (entry)
            entry.forceActiveFocus(reason)
    }
    function focusedIndex() {
        for (let i = 0; i < entryRepeater.count; ++i) {
            const entry = entryRepeater.itemAt(i)
            if (entry && entry.activeFocus)
                return i
        }
        return -1
    }
    // The marked entry, else the first enabled one.
    function focusStart() {
        if (menu.focusedIndex() >= 0)
            return
        const marked = entryRepeater.itemAt(menu.currentIndex)
        const index = marked && marked.enabled ? menu.currentIndex : menu.nextEnabled(-1, 1)
        if (index >= 0)
            menu.focusEntry(index, menu.keyMode ? Qt.TabFocusReason : Qt.OtherFocusReason)
    }

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
    // As wide as the longest entry needs (at least `minimumWidth`), but
    // inside the window: there a long entry takes more lines, never cut.
    property int minimumWidth: 240
    FontMetrics { id: entryMetrics; font.pixelSize: Theme.fontSizeBody }
    readonly property real widestEntry: {
        let widest = 0
        for (const text of menu.entries) {
            widest = Math.max(widest, entryMetrics.advanceWidth(String(text)))
        }
        return Math.ceil(widest)
    }
    width: {
        const room = menu.parent ? menu.parent.width - 2 * Theme.spacingSmall : 100000
        // Text, the entry's padding, the check mark's room, the menu's padding.
        const wanted = menu.widestEntry + 2 * Theme.spacing + Theme.spacingLarge + 2 * Theme.spacingTiny + 2
        return Math.min(room, Math.max(menu.minimumWidth, wanted))
    }
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

    onAboutToShow: {
        const window = menu.parent ? menu.parent.Window.window : null
        const focused = window ? window.activeFocusItem : null
        menu.previousFocus = focused
        // Opened by Enter, the Menu key or a gamepad: the focused control
        // shows keyboard focus.
        menu.keyMode = focused !== null && focused.visualFocus === true
        reposition()
    }
    // As soon as it is visible, so a quick arrow already moves in it.
    onVisibleChanged: if (visible) Qt.callLater(menu.focusStart)
    onOpened: menu.focusStart()
    onClosed: {
        const back = menu.returnFocusTo || menu.previousFocus
        menu.previousFocus = null
        if (!back || !back.visible)
            return
        const window = back.Window.window
        const now = window ? window.activeFocusItem : null
        // Chosen entries can move the focus (a new page): leave that.
        const lost = !now || now === window.contentItem || now === menu.parent
                     || (menu.contentItem && menu.contentItem.parent && menu.contentItem.parent.isAncestorOf
                         && (menu.contentItem.parent === now || menu.contentItem.parent.isAncestorOf(now)))
        if (menu.returnFocusTo || lost)
            back.forceActiveFocus(menu.keyMode ? Qt.TabFocusReason : Qt.OtherFocusReason)
    }
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
            id: entryRepeater
            model: menu.entries
            delegate: ListItem {
                id: entry
                objectName: "popupEntry"
                required property string modelData
                required property int index
                readonly property bool lit: entry.activeFocus && (menu.keyMode || entry.hovered)
                width: list.width
                text: modelData
                wrapText: true
                trailingText: index === menu.currentIndex ? "✓" : ""
                enabled: menu.disabledIndices.indexOf(index) < 0
                // The pointer and the keys light one entry: the one focused.
                onHoveredChanged: if (entry.hovered && menu.opened) entry.forceActiveFocus(Qt.MouseFocusReason)
                Keys.onPressed: (event) => {
                    let to = -1
                    switch (event.key) {
                    case Qt.Key_Up: to = menu.nextEnabled(entry.index, -1); break
                    case Qt.Key_Down: to = menu.nextEnabled(entry.index, 1); break
                    case Qt.Key_Home: to = menu.nextEnabled(-1, 1); break
                    case Qt.Key_End: to = menu.nextEnabled(entryRepeater.count, -1); break
                    case Qt.Key_Left:
                    case Qt.Key_Right:
                        // Nothing beside the entries: kept in the menu.
                        menu.keyMode = true
                        event.accepted = true
                        return
                    default: return
                    }
                    menu.keyMode = true
                    event.accepted = true
                    if (to >= 0)
                        menu.focusEntry(to, Qt.TabFocusReason)
                }
                onClicked: {
                    menu.close()
                    menu.activated(index)
                }
                background: Item {
                    // A line between this entry and the one above.
                    Rectangle {
                        objectName: "popupSeparator"
                        visible: entry.index > 0
                        x: Theme.spacingSmall
                        y: -Math.round(list.spacing / 2) - height / 2
                        width: parent.width - 2 * Theme.spacingSmall
                        height: 1
                        color: Qt.alpha(entry.colors.text, 0.14)
                    }
                    Rectangle {
                        objectName: "popupHighlight"
                        anchors.fill: parent
                        radius: Theme.radius
                        color: entry.down ? Qt.alpha(entry.colors.accent, 0.42)
                             : entry.lit ? Qt.alpha(entry.colors.accent, 0.28) : "transparent"
                        border.width: entry.lit ? 1 : 0
                        border.color: Qt.alpha(entry.colors.accent, 0.7)
                        // A bar at the start, so the lit entry reads at a glance.
                        Rectangle {
                            visible: entry.lit || entry.down
                            x: 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 3
                            height: parent.height - 2 * Theme.spacingSmall
                            radius: 1.5
                            color: entry.colors.accent
                        }
                    }
                    FocusFrame { shown: entry.visualFocus && !entry.lit; inset: 0 }
                }
            }
        }
    }
}
