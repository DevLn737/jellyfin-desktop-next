import QtQuick
import QtQuick.Window
import Jfd.Ui.Theme

// A small label shown below its parent after a short hover, or above it when
// there is no room below in the window (the player's bottom bar).
Rectangle {
    id: tip
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(tip)

    property alias text: label.text
    property bool shown: false

    // Decided when it is about to show.
    property bool above: false
    onShownChanged: {
        if (!shown || !parent || !Window.window) return
        above = parent.mapToItem(null, 0, parent.height).y + Theme.spacingTiny + height > Window.window.height
    }

    y: above ? -height - Theme.spacingTiny : parent.height + Theme.spacingTiny
    anchors.horizontalCenter: parent.horizontalCenter
    width: label.implicitWidth + 2 * Theme.spacingSmall
    height: label.implicitHeight + Theme.spacingTiny * 2
    radius: Theme.radiusSmall
    color: colors.surfaceRaised
    border.color: colors.border
    z: 200
    opacity: 0
    visible: opacity > 0

    states: State {
        when: tip.shown
        PropertyChanges { tip.opacity: 1 }
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
        color: colors.text
        font.pixelSize: Theme.fontSizeSmall
    }
}
