pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// One channel (route "channel/<id>"): its logo, Watch, and what is on now
// and later today. Watch has the focus, so Enter tunes in; Down goes to the
// programmes. When this user may record, the focused programme has a
// Record (or Cancel recording) button, Right reaches it; the Menu key (a
// gamepad's View button) opens all its recording actions and R records it
// or cancels its recording. A dot marks what the server will record.
RoutePage {
    id: page

    readonly property LiveTvSchedule schedule: Library.createSchedule(routeArgument, page)

    title: schedule && schedule.name.length > 0 ? schedule.name : qsTr("Channel")

    Connections {
        target: Library
        function onReconnected() {
            if (page.schedule && page.schedule.errorText.length > 0) page.schedule.reload()
        }
    }

    readonly property bool canRecord: Library.recorder !== null && Library.recorder.available

    RecordMenu {
        id: recordMenu
        returnFocusTo: programmes
    }

    // Hours and minutes in the system's style, never seconds.
    function clock(when) {
        if (!when) return ""
        return Qt.formatTime(when, Qt.locale().timeFormat(Locale.ShortFormat).replace(/[:.\s]*s{1,2}/g, ""))
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLarge

            Image {
                Layout.preferredWidth: 160
                Layout.preferredHeight: 90
                visible: page.schedule !== null && page.schedule.imageUrl.toString().length > 0
                source: page.schedule ? Library.image(page.schedule.imageUrl) : ""
                sourceSize.width: 320
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Heading {
                    objectName: "channelName"
                    Layout.fillWidth: true
                    text: page.schedule ? (page.schedule.number.length > 0
                                           ? page.schedule.number + "  " + page.schedule.name
                                           : page.schedule.name) : ""
                }
                ActionButton {
                    id: watch
                    objectName: "channelWatch"
                    primary: true
                    focus: true
                    iconSource: Icons.play
                    text: qsTr("Watch")
                    enabled: page.routeArgument.length > 0
                    onClicked: Library.playChannel(page.routeArgument)
                    KeyNavigation.down: programmes
                }
            }
        }

        ListView {
            id: programmes
            objectName: "channelProgrammes"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingTiny
            model: page.schedule
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            KeyNavigation.up: watch
            // What is on now is current once the list fills, so Down from
            // Watch lands on it.
            onCountChanged: if (currentIndex < 0 && count > 0) currentIndex = 0

            delegate: Rectangle {
                id: entry
                required property int index
                required property string title
                required property string episodeTitle
                required property string overview
                required property var start
                required property var end
                required property bool onNow
                required property real progress
                required property string programId
                required property bool isSeries
                required property string timerId
                required property string seriesTimerId
                readonly property var program: ({ id: entry.programId, timerId: entry.timerId,
                                                   seriesTimerId: entry.seriesTimerId, isSeries: entry.isSeries })
                readonly property var recordActions: page.canRecord ? recordMenu.actionsFor(entry.program) : []

                width: ListView.view.width
                implicitHeight: Math.max(body.implicitHeight, recordButton.visible ? recordButton.implicitHeight : 0)
                                + 2 * Theme.spacingSmall
                radius: Theme.radius
                color: ListView.isCurrentItem && ListView.view.activeFocus ? Theme.surfaceHover : Theme.surface
                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, %2").arg(page.clock(entry.start)).arg(entry.title)

                // What is on now plays on Enter.
                Keys.onReturnPressed: if (entry.onNow) Library.playChannel(page.routeArgument)
                Keys.onEnterPressed: if (entry.onNow) Library.playChannel(page.routeArgument)
                Keys.onPressed: (event) => {
                    event.accepted = false
                    if (event.modifiers !== Qt.NoModifier || entry.recordActions.length === 0) return
                    if (event.key === Qt.Key_Menu) {
                        event.accepted = recordMenu.openFor(recordButton, entry.program)
                    } else if (event.key === Qt.Key_R) {
                        recordMenu.runFor(entry.program, entry.recordActions[0].name)
                        event.accepted = true
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        entry.ListView.view.currentIndex = entry.index
                        entry.ListView.view.forceActiveFocus()
                    }
                }

                FocusFrame {
                    shown: entry.ListView.isCurrentItem && entry.ListView.view.activeFocus
                }

                // Record or Cancel recording: on the current programme and on
                // any the server records.
                ActionButton {
                    id: recordButton
                    objectName: "channelRecord"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    visible: entry.recordActions.length > 0
                             && (entry.ListView.isCurrentItem || entry.timerId.length > 0)
                    text: entry.recordActions.length > 0 ? entry.recordActions[0].text : ""
                    enabled: !(Library.recorder && Library.recorder.busy)
                    onClicked: recordMenu.runFor(entry.program, entry.recordActions[0].name)
                }

                ColumnLayout {
                    id: body
                    anchors.left: parent.left
                    anchors.right: recordButton.visible ? recordButton.left : parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Theme.spacing
                    anchors.rightMargin: Theme.spacing
                    spacing: Theme.spacingTiny

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Theme.spacing
                        Text {
                            text: qsTr("%1–%2").arg(page.clock(entry.start)).arg(page.clock(entry.end))
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                        }
                        Text {
                            objectName: "programmeTitle"
                            Layout.fillWidth: true
                            text: entry.title
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: entry.onNow ? Theme.fontWeightBold : Font.Normal
                            elide: Text.ElideRight
                        }
                        Text {
                            visible: entry.onNow
                            text: qsTr("On now")
                            color: Theme.accent
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Rectangle {
                            objectName: "channelRecordingMark"
                            visible: entry.timerId.length > 0
                            Layout.preferredWidth: 10
                            Layout.preferredHeight: 10
                            radius: 5
                            color: entry.seriesTimerId.length > 0 ? "transparent" : Theme.error
                            border.color: Theme.error
                            border.width: 2
                        }
                        Text {
                            visible: entry.timerId.length > 0
                            text: entry.seriesTimerId.length > 0 ? qsTr("Series recording") : qsTr("Recording")
                            color: Theme.error
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 3
                        visible: entry.onNow
                        radius: 1
                        color: Theme.border
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, entry.progress))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.progress
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: [entry.episodeTitle, entry.overview].filter((s) => s && s.length > 0).join(" — ")
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.schedule !== null && page.schedule.loading && programmes.count === 0
            }
            StateMessage {
                objectName: "channelState"
                anchors.centerIn: parent
                width: parent.width
                visible: page.schedule !== null && !page.schedule.loading
                         && (page.schedule.errorText.length > 0 || page.schedule.count === 0)
                kind: page.schedule && page.schedule.errorText.length > 0
                      ? (Library.online ? "error" : "offline") : "empty"
                description: page.schedule && page.schedule.errorText.length > 0 ? page.schedule.errorText
                             : qsTr("No programme information")
                onRetry: page.schedule.reload()
            }
        }
    }
}
