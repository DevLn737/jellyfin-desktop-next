import QtQuick
import Jfd.Ui.Theme

// Three pulsing dots while something loads. Static with reduced motion.
Row {
    id: root
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(root)

    property bool running: visible

    spacing: Theme.spacingSmall
    Accessible.role: Accessible.Animation
    Accessible.name: qsTr("Loading")

    Repeater {
        model: 3
        Rectangle {
            id: dot
            required property int index
            width: 10
            height: 10
            radius: 5
            color: colors.accent
            opacity: 0.3

            SequentialAnimation on opacity {
                running: root.running
                loops: Animation.Infinite
                PauseAnimation { duration: dot.index * Theme.durationSlow / 2 }
                NumberAnimation { to: 1; duration: Theme.durationSlow }
                NumberAnimation { to: 0.3; duration: Theme.durationSlow }
                PauseAnimation { duration: (2 - dot.index) * Theme.durationSlow / 2 }
            }
        }
    }
}
