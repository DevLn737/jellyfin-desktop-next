pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// A panel listing one kind of track. `tracks` are PlayerSession track
// entries; `allowOff` adds an "Off" row (subtitles). Emits the chosen
// stream index, -1 for off.
Rectangle {
    id: menu

    property string heading
    property var tracks: []
    property int currentIndex: -1
    property bool allowOff: false

    signal chosen(int streamIndex)

    readonly property var rows: allowOff ? [{ index: -1, off: true }].concat(tracks) : tracks

    function label(track) {
        if (track.off) return qsTr("Off")
        const parts = []
        if (track.title.length > 0) parts.push(track.title)
        else if (track.language.length > 0) parts.push(track.language)
        else parts.push(qsTr("Track %1").arg(track.index))
        if (track.isForced) parts.push(qsTr("forced"))
        if (track.isExternal) parts.push(qsTr("external"))
        return parts.join(" · ")
    }

    width: 340
    height: Math.min(column.implicitHeight + 2 * Theme.spacing, 420)
    radius: Theme.radius
    color: Overlay.surface
    border.color: Overlay.border

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        contentHeight: column.implicitHeight
        clip: true

        Column {
            id: column
            width: parent.width
            spacing: Theme.spacingTiny

            Heading {
                text: menu.heading
                font.pixelSize: Theme.fontSizeLarge
            }

            Repeater {
                model: menu.rows
                delegate: ListItem {
                    required property var modelData
                    width: column.width
                    text: menu.label(modelData)
                    subtitle: modelData.off ? "" : modelData.codec
                    iconSource: modelData.index === menu.currentIndex ? Icons.check : ""
                    onClicked: menu.chosen(modelData.index)
                }
            }

            Text {
                visible: menu.rows.length === 0
                text: qsTr("No tracks")
                color: Overlay.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }
}
