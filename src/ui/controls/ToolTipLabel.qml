import QtQuick
import QtQuick.Window
import Jfd.Ui.Theme
import "ToolTips.js" as ToolTips

// A small label shown below its parent after a short hover, or above it when
// there is no room below in the window (the player's bottom bar). The label
// itself is drawn on the window's content item, kept inside the window, so
// lists that clip and later siblings cannot cut or cover it.
Item {
    id: tip
    // Colours of the nearest colour scope (the video player has its own),
    // read where the tip is declared.
    readonly property var colors: Theme.colorsFor(tip)

    property alias text: label.text
    property bool shown: false
    readonly property Item target: parent
    readonly property Item overlay: Window.window ? Window.window.contentItem : null
    readonly property real margin: Theme.spacingTiny

    width: 0
    height: 0

    function place() {
        const overlay = bubble.parent
        if (!overlay || !tip.target) return
        const origin = tip.target.mapToItem(overlay, 0, 0)
        const below = origin.y + tip.target.height + tip.margin
        bubble.above = below + bubble.height > overlay.height
        bubble.y = bubble.above ? origin.y - bubble.height - tip.margin : below
        const centred = origin.x + (tip.target.width - bubble.width) / 2
        bubble.x = Math.max(tip.margin, Math.min(overlay.width - bubble.width - tip.margin, centred))
    }

    // One tooltip at a time: a new one takes over at once, without a fade.
    // The first waits a moment; one that follows within a second of the
    // last does not (moving the pointer along a row of buttons).
    property bool active: false
    function takeOver() {
        if (ToolTips.current && ToolTips.current !== tip) ToolTips.current.active = false
        ToolTips.current = tip
        active = true
        place()
    }
    function release() {
        if (ToolTips.current === tip) {
            ToolTips.current = null
            ToolTips.lastHidden = Date.now()
        }
        delay.stop()
        active = false
    }
    onShownChanged: {
        if (!shown) {
            release()
            return
        }
        if (ToolTips.current !== null || Date.now() - ToolTips.lastHidden < 1000) takeOver()
        else delay.restart()
    }
    Component.onDestruction: release()

    Timer {
        id: delay
        interval: 450
        onTriggered: if (tip.shown) tip.takeOver()
    }

    Rectangle {
        id: bubble
        objectName: "toolTip"
        // Below or above its parent, decided on showing.
        property bool above: false

        parent: tip.overlay ? tip.overlay : tip
        width: label.implicitWidth + 2 * Theme.spacingSmall
        height: label.implicitHeight + Theme.spacingTiny * 2
        onWidthChanged: if (tip.shown) tip.place()
        radius: Theme.radiusSmall
        color: tip.colors.surfaceRaised
        border.color: tip.colors.border
        z: 10000
        opacity: 0
        // Gone with its button, when the button hides (an overlay fading out).
        visible: opacity > 0 && tip.visible && tip.text.length > 0

        states: State {
            when: tip.active
            PropertyChanges { bubble.opacity: 1 }
        }
        transitions: Transition {
            to: "*"
            NumberAnimation { property: "opacity"; duration: tip.active ? Theme.durationFast : 0 }
        }

        Text {
            id: label
            anchors.centerIn: parent
            color: tip.colors.text
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
