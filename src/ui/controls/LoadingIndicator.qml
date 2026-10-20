import QtQuick
import Jfd.Ui.Theme

// Three pulsing dots while something loads. Static with reduced motion.
Row {
    id: root

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
            color: Theme.accent
            opacity: 0.3

            SequentialAnimation on opacity {
                running: root.running && !Theme.reduceMotion
                loops: Animation.Infinite
                PauseAnimation { duration: dot.index * 160 }
                NumberAnimation { to: 1; duration: 320 }
                NumberAnimation { to: 0.3; duration: 320 }
                PauseAnimation { duration: (2 - dot.index) * 160 }
            }
        }
    }
}
