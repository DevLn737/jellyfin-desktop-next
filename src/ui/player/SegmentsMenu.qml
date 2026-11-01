pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The file's media segments (intro, recap, credits ...) with their times;
// a click jumps to the start of one. `segments` are PlayerSession segments:
// {type, start, end} in seconds.
Rectangle {
    id: panel

    property var segments: []
    property real position: 0

    signal chosen(real start)

    function name(type) {
        return type === "Intro" ? qsTr("Intro")
            : type === "Outro" ? qsTr("Credits")
            : type === "Recap" ? qsTr("Recap")
            : type === "Preview" ? qsTr("Preview")
            : type === "Commercial" ? qsTr("Commercial")
            : qsTr("Segment")
    }

    function clock(seconds) {
        const total = Math.max(0, Math.floor(seconds))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const pad = (n) => n < 10 ? "0" + n : "" + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    width: 340
    height: column.implicitHeight + 2 * Theme.spacing
    radius: Theme.radius
    color: Overlay.surface
    border.color: Overlay.border

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacing
        spacing: Theme.spacingTiny

        Heading {
            text: qsTr("Segments")
            font.pixelSize: Theme.fontSizeLarge
        }
        Text {
            visible: panel.segments.length === 0
            width: parent.width
            text: qsTr("This video has no marked segments.")
            color: Overlay.textSecondary
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }
        Repeater {
            model: panel.segments
            delegate: ListItem {
                required property var modelData
                objectName: "segmentRow"
                width: column.width
                text: panel.name(modelData.type)
                trailingText: panel.clock(modelData.start) + " – " + panel.clock(modelData.end)
                iconSource: panel.position >= modelData.start && panel.position < modelData.end ? Icons.check : ""
                onClicked: panel.chosen(modelData.start)
            }
        }
    }
}
