pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Audio and subtitles in one panel: the audio tracks, then the subtitles
// with "Off", each with its codec on the right and a check on the one
// playing, and at the bottom "Subtitle style and delay" (the playback
// settings). `tracks` lists are PlayerSession track entries. `focusSubtitles`
// puts the focus on the subtitles part first (opened as Subtitles).
Rectangle {
    id: menu

    property var audioTracks: []
    property var subtitleTracks: []
    property int audioIndex: -1
    property int subtitleIndex: -1
    property bool focusSubtitles: false

    signal audioChosen(int streamIndex)
    signal subtitleChosen(int streamIndex)
    signal tuneRequested()

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

    // Where the focus goes when the menu opens (TV mode).
    function focusFirst() {
        const target = focusSubtitles ? subtitleRows.itemAt(0) : audioRows.itemAt(0)
        if (target)
            target.forceActiveFocus(Qt.TabFocusReason)
    }

    objectName: "tracksMenu"
    width: 380
    height: Math.min(column.implicitHeight + 2 * Theme.spacing, 520)
    radius: Theme.radiusLarge
    color: Overlay.surface
    border.color: Overlay.border

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.margins: Theme.spacing
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column
            width: parent.width
            spacing: 2

            SectionLabel { text: qsTr("Audio") }
            Repeater {
                id: audioRows
                model: menu.audioTracks
                delegate: TrackRow {
                    objectName: "audioTrack_" + index
                    width: column.width
                    text: menu.label(modelData)
                    selected: modelData.index === menu.audioIndex
                    onClicked: menu.audioChosen(modelData.index)
                }
            }
            Text {
                visible: menu.audioTracks.length === 0
                leftPadding: Theme.spacing
                text: qsTr("No tracks")
                color: Overlay.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }

            Rule { width: column.width }
            SectionLabel { text: qsTr("Subtitles") }
            Repeater {
                id: subtitleRows
                model: [{ index: -1, off: true, codec: "" }].concat(menu.subtitleTracks)
                delegate: TrackRow {
                    objectName: "subtitleTrack_" + index
                    width: column.width
                    text: menu.label(modelData)
                    selected: modelData.index === menu.subtitleIndex
                    onClicked: menu.subtitleChosen(modelData.index)
                }
            }

            Rule { width: column.width }
            ListItem {
                objectName: "subtitleTune"
                width: column.width
                text: qsTr("Subtitle style and delay")
                trailingText: "›"
                onClicked: menu.tuneRequested()
            }
        }
    }

    component SectionLabel: Text {
        leftPadding: Theme.spacing
        topPadding: Theme.spacingSmall
        bottomPadding: Theme.spacingTiny
        color: Overlay.textSecondary
        font.pixelSize: Theme.fontSizeSmall - 1
        font.weight: Theme.fontWeightBold
        font.letterSpacing: 1.2
        font.capitalization: Font.AllUppercase
    }

    component Rule: Rectangle {
        height: 1
        color: Overlay.border
    }

    component TrackRow: ListItem {
        required property var modelData
        required property int index
        property bool selected: false
        trailingText: modelData.off ? "" : (modelData.codec || "").toUpperCase()
        iconSource: selected ? Icons.check : ""
        reserveIcon: true
        highlighted: selected
    }
}
