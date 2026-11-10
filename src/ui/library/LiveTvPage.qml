pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Live TV (route "livetv"): every channel with what is on now, how far it
// is, and what comes next. Enter or a click tunes in; the channel plays on
// this device only. The programme guide grid comes later (3.7a, part 2).
RoutePage {
    id: page

    readonly property LiveTvChannels channels: Library.liveTv

    title: qsTr("Live TV")

    Component.onCompleted: if (channels) channels.reload()

    Connections {
        target: Library
        function onReconnected() {
            if (page.channels && page.channels.errorText.length > 0) page.channels.reload()
        }
    }

    function clock(when) {
        return when ? Qt.formatTime(when, Locale.ShortFormat) : ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Heading {
                Layout.fillWidth: true
                text: qsTr("Live TV")
            }
            Text {
                objectName: "liveTvCount"
                visible: page.channels !== null && page.channels.count > 0
                text: page.channels ? qsTr("%n channel(s)", "", page.channels.count) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        ListView {
            id: list
            objectName: "liveTvChannels"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            spacing: Theme.spacingTiny
            model: page.channels
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds

            delegate: ChannelRow {
                width: ListView.view.width
            }

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.channels !== null && page.channels.loading && list.count === 0
            }
            StateMessage {
                objectName: "liveTvState"
                anchors.centerIn: parent
                width: parent.width
                visible: page.channels !== null && !page.channels.loading
                         && (page.channels.errorText.length > 0 || page.channels.count === 0)
                kind: page.channels && page.channels.errorText.length > 0
                      ? (Library.online ? "error" : "offline") : "empty"
                description: page.channels && page.channels.errorText.length > 0 ? page.channels.errorText
                             : qsTr("The server has no Live TV channels for you.")
                onRetry: page.channels.reload()
            }
        }
    }

    component ChannelRow: FocusScope {
        id: row

        required property int index
        required property string channelId
        required property string name
        required property string number
        required property url imageUrl
        required property string nowTitle
        required property var nowStart
        required property var nowEnd
        required property real nowProgress
        required property string nextTitle
        required property var nextStart

        implicitHeight: Math.max(Theme.minHitSize, body.implicitHeight + 2 * Theme.spacingSmall)
        Accessible.role: Accessible.Button
        Accessible.name: row.nowTitle.length > 0 ? qsTr("%1, now: %2").arg(row.name).arg(row.nowTitle) : row.name
        activeFocusOnTab: true

        function tune() {
            row.ListView.view.currentIndex = row.index
            Library.playChannel(row.channelId)
        }

        Keys.onReturnPressed: tune()
        Keys.onEnterPressed: tune()

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: mouse.containsMouse || row.ListView.isCurrentItem && row.ListView.view.activeFocus
                   ? Theme.surfaceHover : Theme.surface
        }
        FocusFrame {
            shown: row.ListView.isCurrentItem && row.ListView.view.activeFocus
        }
        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.tune()
        }

        RowLayout {
            id: body
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            spacing: Theme.spacing

            Text {
                Layout.preferredWidth: 40
                text: row.number
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                horizontalAlignment: Text.AlignRight
            }
            Image {
                Layout.preferredWidth: 64
                Layout.preferredHeight: 36
                source: Library.image(row.imageUrl)
                sourceSize.width: 128
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            ColumnLayout {
                Layout.preferredWidth: 220
                Layout.fillWidth: false
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: row.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Theme.fontWeightBold
                    elide: Text.ElideRight
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingTiny
                Text {
                    objectName: "nowTitle"
                    Layout.fillWidth: true
                    text: row.nowTitle.length > 0
                          ? qsTr("%1–%2  %3").arg(page.clock(row.nowStart)).arg(page.clock(row.nowEnd)).arg(row.nowTitle)
                          : qsTr("No programme information")
                    color: row.nowTitle.length > 0 ? Theme.text : Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    elide: Text.ElideRight
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 3
                    visible: row.nowTitle.length > 0
                    radius: 1
                    color: Theme.border
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, row.nowProgress))
                        height: parent.height
                        radius: parent.radius
                        color: Theme.progress
                    }
                }
                Text {
                    objectName: "nextTitle"
                    Layout.fillWidth: true
                    visible: row.nextTitle.length > 0
                    text: qsTr("Next at %1: %2").arg(page.clock(row.nextStart)).arg(row.nextTitle)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    elide: Text.ElideRight
                }
            }
        }
    }
}
