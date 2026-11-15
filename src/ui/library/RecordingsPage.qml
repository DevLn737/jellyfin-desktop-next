pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Recordings (route "recordings"): what the server has recorded, and below
// it what it is to record. A finished recording plays like any video, with
// its time line (Enter or a click); Delete removes it and its file after a
// question. One recording now has no length yet (the server streams it as
// a live source) and is not played from here; its button stops it, after a
// question. A scheduled recording or a series recording is cancelled with
// its button.
//
// Keys and a gamepad: Up and Down move through a list and on to the next;
// Right reaches a row's button, Left goes back; Delete (the key) asks to
// delete the focused recording.
RoutePage {
    id: page

    readonly property LiveTvRecordings recordings: Library.createRecordings(page)
    readonly property LiveTvRecorder recorder: Library.recorder

    title: qsTr("Recordings")

    Component.onCompleted: {
        if (recordings) recordings.reload()
        if (recorder && recorder.available) recorder.reload()
        recordingList.forceActiveFocus()
    }

    // A list read again (or a deleted row) takes its rows and their focus
    // with it: the list keeps the focus it had, at the same place.
    property bool recordingsHadFocus: false
    property bool scheduleHadFocus: false
    function keepFocus(list, had, index) {
        if (!had) return
        list.currentIndex = Math.min(index, list.count - 1)
        list.forceActiveFocus()
    }
    property int recordingsIndex: 0
    property int scheduleIndex: 0
    Connections {
        target: page.recordings
        function onModelAboutToBeReset() {
            page.recordingsHadFocus = recordingList.activeFocus
            page.recordingsIndex = Math.max(0, recordingList.currentIndex)
        }
        function onModelReset() { page.keepFocus(recordingList, page.recordingsHadFocus, page.recordingsIndex) }
        function onRemoved() {
            if (!scheduleList.activeFocus) recordingList.forceActiveFocus()
        }
    }
    Connections {
        target: page.recorder
        function onModelAboutToBeReset() {
            page.scheduleHadFocus = scheduleList.activeFocus
            page.scheduleIndex = Math.max(0, scheduleList.currentIndex)
        }
        function onModelReset() { page.keepFocus(scheduleList, page.scheduleHadFocus, page.scheduleIndex) }
    }

    Connections {
        target: Library
        function onReconnected() {
            if (page.recordings && page.recordings.errorText.length > 0) page.recordings.reload()
            if (page.recorder && page.recorder.errorText.length > 0) page.recorder.reload()
        }
    }

    function valid(when) { return !!when && !isNaN(when.getTime()) }
    // Hours and minutes in the system's style, never seconds.
    function clock(when) {
        if (!valid(when)) return ""
        return Qt.formatTime(when, Qt.locale().timeFormat(Locale.ShortFormat).replace(/[:.\s]*s{1,2}/g, ""))
    }
    function day(when) {
        if (!valid(when)) return ""
        return Qt.formatDate(when, Locale.ShortFormat) + " " + clock(when)
    }

    // Asks before a recording goes: its file goes with it.
    function askDelete(recordingId, title) {
        confirmDelete.recordingId = recordingId
        confirmDelete.recordingTitle = title
        confirmDelete.returnTo = page.Window.activeFocusItem
        confirmDelete.open()
    }
    // And before one recording now stops.
    function askStop(timerId, title) {
        confirmStop.timerId = timerId
        confirmStop.recordingTitle = title
        confirmStop.returnTo = page.Window.activeFocusItem
        confirmStop.open()
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacing

        Heading {
            Layout.fillWidth: true
            text: qsTr("Recordings")
        }

        ListView {
            id: recordingList
            objectName: "recordingList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            spacing: Theme.spacingTiny
            model: page.recordings
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            // The first row is current once the list fills.
            onCountChanged: if (currentIndex < 0 && count > 0) currentIndex = 0
            // Down from the last recording reaches the schedule.
            Keys.onDownPressed: (event) => {
                if (recordingList.currentIndex >= recordingList.count - 1 && scheduleList.count > 0)
                    scheduleList.forceActiveFocus()
                else
                    event.accepted = false
            }

            delegate: RecordingRow {
                width: ListView.view.width
            }

            SmoothWheel {}

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.recordings !== null && page.recordings.loading && recordingList.count === 0
            }
            StateMessage {
                objectName: "recordingsState"
                anchors.centerIn: parent
                width: parent.width
                visible: page.recordings !== null && !page.recordings.loading
                         && (page.recordings.errorText.length > 0 || page.recordings.count === 0)
                kind: page.recordings && page.recordings.errorText.length > 0
                      ? (Library.online ? "error" : "offline") : "empty"
                description: page.recordings && page.recordings.errorText.length > 0 ? page.recordings.errorText
                             : qsTr("Nothing has been recorded yet. Record a programme from the guide or a channel's page.")
                onRetry: page.recordings.reload()
            }
        }

        Heading {
            Layout.fillWidth: true
            Layout.topMargin: Theme.spacing
            text: qsTr("Scheduled")
            font.pixelSize: Theme.fontSizeLarge
        }

        ListView {
            id: scheduleList
            objectName: "scheduleList"
            Layout.fillWidth: true
            // As tall as its rows, up to two fifths of the page; the
            // recordings have the rest.
            Layout.preferredHeight: Math.max(Theme.minHitSize * 2, Math.min(contentHeight, page.height * 0.4))
            clip: true
            spacing: Theme.spacingTiny
            model: page.recorder
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            // The first row is current once the list fills.
            onCountChanged: if (currentIndex < 0 && count > 0) currentIndex = 0
            // Up from the first one goes back to the recordings.
            Keys.onUpPressed: (event) => {
                if (scheduleList.currentIndex <= 0 && recordingList.count > 0)
                    recordingList.forceActiveFocus()
                else
                    event.accepted = false
            }

            delegate: TimerRow {
                width: ListView.view.width
            }

            StateMessage {
                objectName: "scheduleState"
                anchors.centerIn: parent
                width: parent.width
                visible: page.recorder !== null && !page.recorder.loading
                         && (page.recorder.errorText.length > 0 || page.recorder.count === 0)
                kind: page.recorder && page.recorder.errorText.length > 0
                      ? (Library.online ? "error" : "offline") : "empty"
                description: page.recorder && page.recorder.errorText.length > 0 ? page.recorder.errorText
                             : qsTr("No recordings are scheduled.")
                onRetry: page.recorder.reload()
            }
        }
    }

    AppDialog {
        id: confirmDelete
        objectName: "confirmDeleteRecording"
        property string recordingId
        property string recordingTitle
        property Item returnTo
        title: qsTr("Delete this recording?")
        text: qsTr("%1 and its file are deleted from the server. This cannot be undone.").arg(recordingTitle)
        acceptText: qsTr("Delete")
        destructive: true
        onAccepted: if (page.recordings) page.recordings.remove(recordingId)
        // The row may have gone meanwhile (the list read again).
        onClosed: if (returnTo) returnTo.forceActiveFocus(); else recordingList.forceActiveFocus()
    }

    AppDialog {
        id: confirmStop
        objectName: "confirmStopRecording"
        property string timerId
        property string recordingTitle
        property Item returnTo
        title: qsTr("Stop this recording?")
        text: qsTr("%1 stops recording now. What has been recorded is kept.").arg(recordingTitle)
        acceptText: qsTr("Stop recording")
        rejectText: qsTr("Keep recording")
        destructive: true
        onAccepted: if (page.recorder) page.recorder.cancel(timerId)
        // The row may have gone meanwhile (the list read again).
        onClosed: if (returnTo) returnTo.forceActiveFocus(); else recordingList.forceActiveFocus()
    }

    component RecordingRow: FocusScope {
        id: row

        required property int index
        required property string recordingId
        required property string title
        required property string subtitle
        required property string channelName
        required property var date
        required property real runTimeTicks
        required property real positionTicks
        required property bool played
        required property bool inProgress
        required property string timerId
        required property url imageUrl

        readonly property bool current: ListView.isCurrentItem && ListView.view.activeFocus

        implicitHeight: Math.max(Theme.minHitSize, body.implicitHeight + 2 * Theme.spacingSmall)
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: row.subtitle.length > 0 ? qsTr("%1, %2").arg(row.title).arg(row.subtitle) : row.title

        function play() {
            row.ListView.view.currentIndex = row.index
            // Still recording: Enter goes to Stop recording.
            if (row.inProgress) {
                if (deleteButton.visible) deleteButton.forceActiveFocus()
                return
            }
            // A recording plays as an item, from where it was left.
            Library.play(row.recordingId, row.played ? 0 : row.positionTicks)
        }
        function remove() {
            if (row.inProgress) {
                if (row.timerId.length > 0) page.askStop(row.timerId, row.title)
            } else {
                page.askDelete(row.recordingId, row.title)
            }
        }

        Keys.onReturnPressed: row.play()
        Keys.onEnterPressed: row.play()
        Keys.onDeletePressed: row.remove()
        KeyNavigation.right: deleteButton.visible ? deleteButton : null

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: mouse.containsMouse || row.current ? Theme.surfaceHover : Theme.surface
        }
        FocusFrame {
            shown: row.current
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.play()
        }

        RowLayout {
            id: body
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            spacing: Theme.spacing

            Image {
                Layout.preferredWidth: 96
                Layout.preferredHeight: 54
                visible: row.imageUrl.toString().length > 0
                source: Library.image(row.imageUrl)
                sourceSize.width: 192
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTiny
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    Rectangle {
                        visible: row.inProgress
                        Layout.preferredWidth: 10
                        Layout.preferredHeight: 10
                        radius: 5
                        color: Theme.error
                    }
                    Text {
                        objectName: "recordingTitle"
                        Layout.fillWidth: true
                        text: row.subtitle.length > 0 ? qsTr("%1 — %2").arg(row.title).arg(row.subtitle) : row.title
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Theme.fontWeightBold
                        elide: Text.ElideRight
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: [row.inProgress ? qsTr("Recording now") : "",
                           page.day(row.date),
                           row.channelName,
                           row.inProgress ? "" : Library.formatRunTime(row.runTimeTicks)]
                          .filter((s) => s && s.length > 0).join(" · ")
                    color: row.inProgress ? Theme.error : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
                // How far it was watched.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 3
                    visible: !row.inProgress && row.positionTicks > 0 && row.runTimeTicks > 0 && !row.played
                    radius: 1
                    color: Theme.border
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, row.positionTicks / row.runTimeTicks))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.progress
                    }
                }
            }
            ActionButton {
                id: deleteButton
                objectName: "recordingDelete"
                visible: !row.inProgress || row.timerId.length > 0
                text: row.inProgress ? qsTr("Stop recording") : qsTr("Delete")
                onClicked: row.remove()
                // Back to the row (its own scope would hand the focus
                // straight back to the button).
                Keys.onLeftPressed: {
                    deleteButton.focus = false
                    row.forceActiveFocus()
                }
            }
        }
    }

    component TimerRow: FocusScope {
        id: timer

        required property int index
        required property string timerId
        required property bool isSeries
        required property string title
        required property string channelName
        required property var start
        required property var end
        required property bool recording

        readonly property bool current: ListView.isCurrentItem && ListView.view.activeFocus

        implicitHeight: Math.max(Theme.minHitSize, line.implicitHeight + 2 * Theme.spacingSmall)
        activeFocusOnTab: true
        Accessible.role: Accessible.ListItem
        Accessible.name: timer.title

        function cancel() {
            if (!page.recorder) return
            if (timer.isSeries) page.recorder.cancelSeries(timer.timerId)
            else if (timer.recording) page.askStop(timer.timerId, timer.title)
            else page.recorder.cancel(timer.timerId)
        }

        Keys.onDeletePressed: timer.cancel()

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: timer.current ? Theme.surfaceHover : Theme.surface
        }
        FocusFrame {
            shown: timer.current
        }

        RowLayout {
            id: line
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            spacing: Theme.spacing

            Rectangle {
                Layout.preferredWidth: 10
                Layout.preferredHeight: 10
                radius: 5
                color: timer.isSeries ? "transparent" : Theme.error
                border.color: Theme.error
                border.width: 2
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    objectName: "timerTitle"
                    Layout.fillWidth: true
                    text: timer.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.fontWeightBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: [timer.isSeries ? qsTr("Every new episode")
                                          : timer.recording ? qsTr("Recording now")
                                                            : page.valid(timer.start) ? qsTr("%1–%2").arg(page.day(timer.start)).arg(page.clock(timer.end)) : "",
                           timer.channelName].filter((s) => s && s.length > 0).join(" · ")
                    color: timer.recording ? Theme.error : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
            }
            ActionButton {
                id: cancelButton
                objectName: "timerCancel"
                text: timer.isSeries ? qsTr("Cancel series recording")
                                     : timer.recording ? qsTr("Stop recording") : qsTr("Cancel recording")
                enabled: !(page.recorder && page.recorder.busy)
                onClicked: timer.cancel()
                Keys.onLeftPressed: {
                    cancelButton.focus = false
                    timer.forceActiveFocus()
                }
            }
        }

        // Enter goes to the button (there is nothing to play yet), so
        // cancelling takes a second Enter.
        Keys.onReturnPressed: cancelButton.forceActiveFocus()
        Keys.onEnterPressed: cancelButton.forceActiveFocus()
        KeyNavigation.right: cancelButton
    }
}
