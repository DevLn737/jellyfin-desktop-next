import QtQuick
import Jfd.Ui.Theme

// A small label shown below its parent after a short hover.
Rectangle {
    id: tip

    property alias text: label.text
    property bool shown: false

    anchors.top: parent.bottom
    anchors.topMargin: Theme.spacingTiny
    anchors.horizontalCenter: parent.horizontalCenter
    width: label.implicitWidth + 2 * Theme.spacingSmall
    height: label.implicitHeight + Theme.spacingTiny * 2
    radius: Theme.radiusSmall
    color: Theme.surfaceRaised
    border.color: Theme.border
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
        color: Theme.text
        font.pixelSize: Theme.fontSizeSmall
    }
}
