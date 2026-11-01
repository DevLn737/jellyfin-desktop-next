pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// What plays now: the big cover, title, a seek bar, the controls and the
// queue, where a click jumps to a track and the menu takes one out.
RoutePage {
    id: page

    readonly property var track: MusicSession.currentTrack

    title: qsTr("Now playing")

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.spacingHuge
        spacing: Theme.spacingHuge

        ColumnLayout {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            spacing: Theme.spacing

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: width
                radius: Theme.radius
                color: Theme.surfaceRaised
                clip: true
                Image {
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: Library.image(Music.coverUrl(page.track.imageItemId || "", page.track.imageTag || "", 360))
                }
            }
            Text {
                objectName: "nowTitle"
                Layout.fillWidth: true
                text: page.track.title || ""
                color: Theme.text
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Theme.fontWeightBold
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: page.track.artist || ""
                color: Theme.accent
                font.pixelSize: Theme.fontSizeLarge
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: page.track.album || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
                TapHandler {
                    enabled: (page.track.albumId || "").length > 0
                    onTapped: Navigator.navigate("album/" + page.track.albumId)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSmall
                Text {
                    text: Music.formatDuration(MusicSession.position)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }
                ValueSlider {
                    id: seek
                    objectName: "nowSeek"
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, MusicSession.duration)
                    enabled: MusicSession.active
                    // The thumb follows the player except while it is dragged.
                    value: MusicSession.position
                    onMoved: MusicSession.seek(value)
                    Accessible.name: qsTr("Position")
                }
                Text {
                    text: Music.formatDuration(MusicSession.duration)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                spacing: Theme.spacingSmall
                IconButton {
                    objectName: "nowShuffle"
                    iconSource: MusicIcons.shuffle
                    toolTipText: MusicSession.shuffle ? qsTr("Shuffle: on") : qsTr("Shuffle: off")
                    opacity: MusicSession.shuffle ? 1 : 0.55
                    onClicked: MusicSession.shuffle = !MusicSession.shuffle
                }
                IconButton {
                    objectName: "nowPrevious"
                    iconSource: MusicIcons.previous
                    toolTipText: qsTr("Previous")
                    onClicked: MusicSession.previous()
                }
                IconButton {
                    objectName: "nowPlay"
                    iconSource: MusicSession.playing ? MusicIcons.pause : Icons.play
                    toolTipText: MusicSession.playing ? qsTr("Pause") : qsTr("Play")
                    onClicked: MusicSession.active ? MusicSession.togglePause() : MusicSession.resume()
                }
                IconButton {
                    objectName: "nowNext"
                    iconSource: MusicIcons.next
                    toolTipText: qsTr("Next")
                    enabled: MusicSession.hasNext
                    onClicked: MusicSession.next()
                }
                IconButton {
                    objectName: "nowRepeat"
                    iconSource: MusicSession.repeat === "track" ? MusicIcons.repeatOne : MusicIcons.repeat
                    toolTipText: MusicSession.repeat === "none" ? qsTr("Repeat: off")
                        : MusicSession.repeat === "queue" ? qsTr("Repeat: queue") : qsTr("Repeat: track")
                    opacity: MusicSession.repeat === "none" ? 0.55 : 1
                    onClicked: MusicSession.repeat = MusicSession.repeat === "none" ? "queue"
                        : MusicSession.repeat === "queue" ? "track" : "none"
                }
            }
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                text: MusicSession.error
                color: Theme.error
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.Wrap
            }
            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.spacing

            RowLayout {
                Layout.fillWidth: true
                SectionHeader {
                    Layout.fillWidth: true
                    text: qsTr("Queue")
                }
                ActionButton {
                    objectName: "nowClear"
                    text: qsTr("Clear")
                    enabled: MusicSession.queueSize > 0
                    onClicked: {
                        MusicSession.clear()
                        Navigator.back()
                    }
                }
            }

            ListView {
                id: queue
                objectName: "nowQueue"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                model: MusicSession.queue
                currentIndex: MusicSession.currentIndex
                delegate: ListItem {
                    id: entry
                    required property int index
                    required property string title
                    required property string artist
                    required property double duration
                    required property bool isCurrent
                    width: ListView.view.width
                    text: (isCurrent ? "\u25B6 " : "") + title
                    subtitle: artist
                    trailingText: Music.formatDuration(duration)
                    onClicked: MusicSession.jumpTo(index)
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: MusicSession.removeAt(entry.index)
                    }
                    Keys.onDeletePressed: MusicSession.removeAt(entry.index)
                }
            }
        }
    }
}
