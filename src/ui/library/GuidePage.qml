pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The programme guide (route "guide"): a row per channel, programmes along
// a time line from the half hour before now. Only the rows on screen and
// the programmes in view (and an hour either side) are made.
//
// Keys (and a gamepad's d-pad, A, B, LB and RB): arrows move between
// programmes and channels, Page Up and Page Down a screen of time earlier
// and later, Home back to now, Enter watches what is on now or opens the
// channel's page for a programme to come. When this user may record, the
// Menu key (a gamepad's View button) opens the focused programme's
// recording actions and R records it or cancels its recording; a dot marks
// what the server will record.
RoutePage {
    id: page

    readonly property LiveTvGuide guide: Library.createGuide(page)
    // Half an hour is 180 px.
    readonly property real minuteWidth: 6
    readonly property int channelColumn: 240
    readonly property int rowHeight: Math.max(Theme.minHitSize, 64)
    // The view's left edge in pixels along the time line, animated.
    property real offset: guide ? guide.viewStartMinutes * minuteWidth : 0
    Behavior on offset { NumberAnimation { duration: Theme.durationNormal; easing.type: Easing.OutCubic } }

    title: qsTr("Guide")
    focus: true

    Component.onCompleted: {
        if (!guide) return
        guide.viewMinutes = page.viewMinutesFor(strip.width)
        guide.reload()
    }

    Connections {
        target: Library
        function onReconnected() {
            if (page.guide && page.guide.errorText.length > 0) page.guide.reload()
        }
    }

    Connections {
        target: page.guide
        function onFocusChanged() {
            if (page.guide.focusRow >= 0 && page.guide.focusRow < rows.count)
                rows.positionViewAtIndex(page.guide.focusRow, ListView.Contain)
        }
    }

    function viewMinutesFor(pixels) {
        return Math.max(30, Math.floor(pixels / minuteWidth))
    }

    function timeAt(minutes) {
        if (!guide) return ""
        return clock(new Date(guide.rangeStart.getTime() + minutes * 60000))
    }

    // Hours and minutes in the system's style, never seconds.
    function clock(when) {
        if (!when) return ""
        return Qt.formatTime(when, Qt.locale().timeFormat(Locale.ShortFormat).replace(/[:.\s]*s{1,2}/g, ""))
    }

    readonly property var focused: guide ? guide.focusedProgram : ({})
    readonly property bool focusedOnNow: guide !== null && focused.id !== undefined
        && focused.offset <= guide.nowMinutes && guide.nowMinutes < focused.offset + focused.length
    // Recording actions for the focused programme: not one that has ended.
    readonly property bool canRecordFocused: Library.recorder !== null && Library.recorder.available
        && guide !== null && focused.id !== undefined && focused.offset + focused.length > guide.nowMinutes

    RecordMenu {
        id: recordMenu
        program: page.canRecordFocused ? page.focused : ({})
        returnFocusTo: page
    }

    // Enter: what is on the air plays; anything else opens the channel.
    function activate() {
        if (!guide || guide.focusedChannelId.length === 0) return
        const onNowSlot = focused.id === undefined
            && Math.floor(guide.focusMinutes / 30) === Math.floor(guide.nowMinutes / 30)
        if (focusedOnNow || onNowSlot) Library.playChannel(guide.focusedChannelId)
        else Navigator.navigate("channel/" + guide.focusedChannelId)
    }

    Keys.onPressed: (event) => {
        if (!guide || event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) {
            event.accepted = false
            return
        }
        switch (event.key) {
        case Qt.Key_Left: event.accepted = guide.moveFocus(-1, 0); return
        case Qt.Key_Right: event.accepted = guide.moveFocus(1, 0); return
        case Qt.Key_Up: event.accepted = guide.moveFocus(0, -1); return
        case Qt.Key_Down: event.accepted = guide.moveFocus(0, 1); return
        case Qt.Key_PageUp: guide.pageFocus(-1); break
        case Qt.Key_PageDown: guide.pageFocus(1); break
        case Qt.Key_Home: guide.focusNow(); break
        case Qt.Key_Menu:
            event.accepted = page.canRecordFocused && recordMenu.openFor(details)
            return
        case Qt.Key_R:
            if (!page.canRecordFocused) {
                event.accepted = false
                return
            }
            recordMenu.toggle()
            break
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Select:
            page.activate(); break
        default:
            event.accepted = false
            return
        }
        event.accepted = true
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacing

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Heading {
                Layout.fillWidth: true
                text: qsTr("Guide")
            }
            ActionButton {
                objectName: "guideRecordings"
                visible: Library.recorder !== null && Library.recorder.available
                text: qsTr("Recordings")
                focusPolicy: Qt.NoFocus
                onClicked: Navigator.navigate("recordings")
            }
            ActionButton {
                objectName: "guideNow"
                text: qsTr("Now")
                focusPolicy: Qt.NoFocus
                onClicked: if (page.guide) page.guide.focusNow()
            }
            ActionButton {
                text: qsTr("Earlier")
                focusPolicy: Qt.NoFocus
                onClicked: if (page.guide) page.guide.pageFocus(-1)
            }
            ActionButton {
                text: qsTr("Later")
                focusPolicy: Qt.NoFocus
                onClicked: if (page.guide) page.guide.pageFocus(1)
            }
        }

        // The focused programme.
        Column {
            id: details
            objectName: "guideDetails"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.fontSizeLarge * 2 + Theme.fontSizeBody * 3
            spacing: Theme.spacingTiny
            Text {
                objectName: "guideDetailsTitle"
                width: parent.width
                text: page.focused.id !== undefined ? page.focused.title
                                                    : (page.guide && page.guide.count > 0 ? qsTr("No programme information") : "")
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Theme.fontWeightBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: page.focused.id !== undefined
                text: qsTr("%1–%2 · %3").arg(page.clock(page.focused.start)).arg(page.clock(page.focused.end))
                      .arg(page.focused.channelName || "")
                      + (page.focusedOnNow ? "  ·  " + qsTr("On now") : "")
                      + (page.focused.timerId ? "  ·  " + (page.focused.seriesTimerId ? qsTr("Series recording")
                                                                                     : qsTr("Recording"))
                                              : "")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: page.focused.id !== undefined
                text: [page.focused.episodeTitle, page.focused.overview].filter((s) => s && s.length > 0).join(" — ")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }

        // The focused programme's recording actions, for the mouse (the
        // Menu key and R do the same from the keys).
        Row {
            objectName: "guideRecordActions"
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.controlHeight
            spacing: Theme.spacingSmall
            // Its room stays while recording is there, so the grid does not
            // move as the focus passes programmes that have ended.
            visible: Library.recorder !== null && Library.recorder.available
            Repeater {
                model: recordMenu.actions
                delegate: ActionButton {
                    required property var modelData
                    objectName: "guideRecordAction"
                    text: modelData.text
                    focusPolicy: Qt.NoFocus
                    enabled: !(Library.recorder && Library.recorder.busy)
                    onClicked: recordMenu.run(modelData.name)
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: page.canRecordFocused
                text: qsTr("Menu: recording options")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        // The time line over the programmes.
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: Theme.fontSizeBody + Theme.spacing
            clip: true
            Item {
                id: ruler
                x: page.channelColumn
                width: parent.width - page.channelColumn
                height: parent.height
                Repeater {
                    model: page.guide ? Math.ceil(page.guide.viewMinutes / 30) + 2 : 0
                    delegate: Text {
                        required property int index
                        readonly property int minute: Math.floor(page.offset / page.minuteWidth / 30) * 30 + index * 30
                        x: minute * page.minuteWidth - page.offset + Theme.spacingTiny
                        visible: x > -width && x < ruler.width
                        text: page.timeAt(minute)
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ListView {
                id: rows
                objectName: "guideRows"
                anchors.fill: parent
                clip: true
                model: page.guide
                currentIndex: page.guide ? page.guide.focusRow : -1
                spacing: Theme.spacingTiny
                boundsBehavior: Flickable.StopAtBounds
                // Keys belong to the page; the list only shows the rows.
                interactive: true
                keyNavigationEnabled: false
                highlightFollowsCurrentItem: false
                cacheBuffer: page.rowHeight * 2

                delegate: GuideRow {
                    width: ListView.view.width
                }
            }

            // Strip width, for the view's length in minutes.
            Item {
                id: strip
                x: page.channelColumn
                width: parent.width - page.channelColumn
                height: parent.height
                onWidthChanged: if (page.guide) page.guide.viewMinutes = page.viewMinutesFor(width)

                // Now.
                Rectangle {
                    objectName: "guideNowLine"
                    x: page.guide ? page.guide.nowMinutes * page.minuteWidth - page.offset : 0
                    visible: page.guide !== null && page.guide.count > 0 && x >= 0 && x <= parent.width
                    width: 2
                    height: Math.min(parent.height, rows.contentHeight)
                    color: Theme.accent
                }

                WheelHandler {
                    // Shift+wheel, or a sideways scroll, moves along time.
                    acceptedModifiers: Qt.ShiftModifier
                    onWheel: (event) => {
                        if (!page.guide) return
                        const delta = event.angleDelta.y !== 0 ? event.angleDelta.y : event.angleDelta.x
                        page.guide.viewStartMinutes += delta < 0 ? 30 : -30
                    }
                }
                WheelHandler {
                    orientation: Qt.Horizontal
                    onWheel: (event) => {
                        if (!page.guide || event.angleDelta.x === 0) return
                        page.guide.viewStartMinutes += event.angleDelta.x < 0 ? 30 : -30
                    }
                }
            }

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.guide !== null && page.guide.loading && rows.count === 0
            }
            StateMessage {
                objectName: "guideState"
                anchors.centerIn: parent
                width: parent.width
                visible: page.guide !== null && !page.guide.loading
                         && (page.guide.errorText.length > 0 || page.guide.count === 0)
                kind: page.guide && page.guide.errorText.length > 0
                      ? (Library.online ? "error" : "offline") : "empty"
                description: page.guide && page.guide.errorText.length > 0 ? page.guide.errorText
                             : qsTr("The server has no Live TV channels for you.")
                onRetry: page.guide.reload()
            }
        }
    }

    component GuideRow: Item {
        id: row

        required property int index
        required property string channelId
        required property string name
        required property string number
        required property url imageUrl
        required property var programs

        readonly property bool focusedRow: page.guide !== null && page.guide.focusRow === row.index

        height: page.rowHeight

        // The channel: its page on a click.
        Rectangle {
            id: channelCell
            objectName: "guideChannel"
            width: page.channelColumn - Theme.spacingSmall
            height: parent.height
            radius: Theme.radius
            color: row.focusedRow ? Theme.surfaceHover : Theme.surface

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingSmall
                anchors.rightMargin: Theme.spacingSmall
                spacing: Theme.spacingSmall
                Text {
                    Layout.preferredWidth: 32
                    text: row.number
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    horizontalAlignment: Text.AlignRight
                }
                Image {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 28
                    source: Library.image(row.imageUrl)
                    sourceSize.width: 96
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                }
                Text {
                    Layout.fillWidth: true
                    text: row.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.fontWeightBold
                    elide: Text.ElideRight
                }
            }
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: Navigator.navigate("channel/" + row.channelId)
            }
        }

        Item {
            x: page.channelColumn
            width: parent.width - page.channelColumn
            height: parent.height
            clip: true

            // An empty slot with the focus.
            Rectangle {
                visible: row.focusedRow && page.focused.id === undefined
                x: Math.floor(page.guide.focusMinutes / 30) * 30 * page.minuteWidth - page.offset
                width: 30 * page.minuteWidth - 2
                height: parent.height
                radius: Theme.radius
                color: "transparent"
                border.color: Theme.accent
                border.width: Theme.focusRingWidth
            }

            Repeater {
                model: row.programs
                delegate: Rectangle {
                    id: cell
                    required property var modelData
                    readonly property bool isFocused: row.focusedRow && page.focused.id === cell.modelData.id
                    readonly property bool onNow: page.guide !== null
                        && cell.modelData.offset <= page.guide.nowMinutes
                        && page.guide.nowMinutes < cell.modelData.offset + cell.modelData.length

                    objectName: "guideProgram"
                    x: cell.modelData.offset * page.minuteWidth - page.offset
                    width: Math.max(4, cell.modelData.length * page.minuteWidth - 2)
                    height: row.height
                    radius: Theme.radius
                    color: isFocused ? Theme.surfaceHover : (onNow ? Qt.lighter(Theme.surface, 1.15) : Theme.surface)
                    border.color: isFocused ? Theme.accent : "transparent"
                    border.width: isFocused ? Theme.focusRingWidth : 0

                    // The server will record it (a ring for a series).
                    Rectangle {
                        id: mark
                        objectName: "guideRecordingMark"
                        visible: (cell.modelData.timerId || "").length > 0
                        x: Math.max(Theme.spacingSmall, -cell.x + Theme.spacingSmall)
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        radius: 5
                        color: (cell.modelData.seriesTimerId || "").length > 0 ? "transparent" : Theme.error
                        border.color: Theme.error
                        border.width: 2
                        Accessible.role: Accessible.Indicator
                        Accessible.name: (cell.modelData.seriesTimerId || "").length > 0 ? qsTr("Series recording")
                                                                                        : qsTr("Recording")
                    }
                    Text {
                        // The title stays readable when the programme began
                        // before the left edge.
                        x: Math.max(Theme.spacingSmall, -cell.x + Theme.spacingSmall) + (mark.visible ? mark.width + Theme.spacingTiny : 0)
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.max(0, cell.width - x - Theme.spacingSmall)
                        text: cell.modelData.title
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideRight
                    }
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            page.forceActiveFocus()
                            page.guide.focusAt(row.index, Math.max(0, Math.ceil(cell.modelData.offset)))
                            page.activate()
                        }
                    }
                }
            }
        }
    }
}
