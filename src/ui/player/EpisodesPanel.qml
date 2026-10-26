pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The series' episodes, in order, with the one playing marked. `episodes`
// are PlayerSession episode entries; emits the chosen item id.
Rectangle {
    id: panel

    property var episodes: []

    signal chosen(string itemId)

    function label(episode) {
        const code = episode.season > 0 ? qsTr("S%1 E%2").arg(episode.season).arg(episode.number)
                   : episode.number > 0 ? qsTr("E%1").arg(episode.number) : ""
        return code.length > 0 ? code + " · " + episode.name : episode.name
    }

    function currentRow() {
        for (let i = 0; i < episodes.length; ++i) {
            if (episodes[i].current) return i
        }
        return 0
    }

    width: 340
    height: Math.min(column.implicitHeight + 2 * Theme.spacing, 420)
    radius: Theme.radius
    color: Overlay.surface
    border.color: Overlay.border

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: Theme.spacing
        contentHeight: column.implicitHeight
        clip: true

        // Opens scrolled to the episode that plays.
        Component.onCompleted: contentY = Math.max(0, Math.min(
            contentHeight - height, panel.currentRow() * (Theme.controlHeight + Theme.spacingTiny)))

        Column {
            id: column
            width: parent.width
            spacing: Theme.spacingTiny

            Heading {
                text: qsTr("Episodes")
                font.pixelSize: Theme.fontSizeLarge
            }

            Repeater {
                model: panel.episodes
                delegate: ListItem {
                    required property var modelData
                    width: column.width
                    text: panel.label(modelData)
                    iconSource: modelData.current ? Icons.check : ""
                    enabled: !modelData.current
                    opacity: modelData.played && !modelData.current ? 0.6 : 1
                    onClicked: panel.chosen(modelData.id)
                }
            }
        }
    }
}
