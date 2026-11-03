import QtQuick
import QtQuick.Window
import Jfd.Ui.Theme

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
    readonly property Item layer: Window.window ? Window.window.contentItem : null
    readonly property real margin: Theme.spacingTiny

    width: 0
    height: 0

    function place() {
        const layer = bubble.parent
        if (!layer || !tip.target) return
        const origin = tip.target.mapToItem(layer, 0, 0)
        const below = origin.y + tip.target.height + tip.margin
        bubble.above = below + bubble.height > layer.height
        bubble.y = bubble.above ? origin.y - bubble.height - tip.margin : below
        const centred = origin.x + (tip.target.width - bubble.width) / 2
        bubble.x = Math.max(tip.margin, Math.min(layer.width - bubble.width - tip.margin, centred))
    }

    onShownChanged: if (shown) place()

    Rectangle {
        id: bubble
        objectName: "toolTip"
        // Below or above its parent, decided on showing.
        property bool above: false

        parent: tip.layer ? tip.layer : tip
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
            when: tip.shown
            PropertyChanges { bubble.opacity: 1 }
        }
        transitions: Transition {
            SequentialAnimation {
                PauseAnimation { duration: tip.shown ? 500 : 0 }
                NumberAnimation { property: "opacity"; duration: Theme.durationFast }
            }
        }

        Text {
            id: label
            anchors.centerIn: parent
            color: tip.colors.text
            font.pixelSize: Theme.fontSizeSmall
        }
    }
}
