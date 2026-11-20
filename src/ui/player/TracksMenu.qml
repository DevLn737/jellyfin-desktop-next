pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// Audio and subtitles in one panel: the audio tracks beside the subtitles
// (with "Off"), each with its codec on the right and a check on the one
// playing, and at the bottom the subtitles' look and delay (the player
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
    // Two columns side by side; one under the other in a narrow window.
    // The room the page has for the menu.
    property real availableWidth: 1000
    readonly property bool narrow: availableWidth < 2 * 300 + 3 * Theme.spacing
    readonly property real columnWidth: narrow ? 340 : 300
    width: (narrow ? columnWidth : 2 * columnWidth + Theme.spacing) + 2 * Theme.spacing
    height: Math.min(body.implicitHeight + footer.height + 3 * Theme.spacing, 520)
    radius: Theme.radiusLarge
    color: Overlay.surface
    border.color: Overlay.border

    Flickable {
        id: flick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: footer.top
        anchors.margins: Theme.spacing
        anchors.bottomMargin: Theme.spacingSmall
        contentHeight: body.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Grid {
            id: body
            columns: menu.narrow ? 1 : 2
            columnSpacing: Theme.spacing
            rowSpacing: Theme.spacingSmall

            Column {
                id: audioColumn
                width: menu.columnWidth
                spacing: 2

                SectionLabel { text: qsTr("Audio") }
                Repeater {
                    id: audioRows
                    model: menu.audioTracks
                    delegate: TrackRow {
                        objectName: "audioTrack_" + index
                        width: audioColumn.width
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
            }

            Column {
                id: subtitleColumn
                width: menu.columnWidth
                spacing: 2

                SectionLabel { text: qsTr("Subtitles") }
                Repeater {
                    id: subtitleRows
                    model: [{ index: -1, off: true, codec: "" }].concat(menu.subtitleTracks)
                    delegate: TrackRow {
                        objectName: "subtitleTrack_" + index
                        width: subtitleColumn.width
                        text: menu.label(modelData)
                        selected: modelData.index === menu.subtitleIndex
                        onClicked: menu.subtitleChosen(modelData.index)
                    }
                }
            }
        }
    }

    Column {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing
        anchors.topMargin: 0
        spacing: Theme.spacingSmall
        Rule { width: footer.width }
        ListItem {
            objectName: "subtitleTune"
            width: footer.width
            text: qsTr("Subtitle size, position and delay")
            trailingText: "›"
            onClicked: menu.tuneRequested()
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
