pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The strip at the bottom of every page with music in the queue: cover,
// title and artist, previous / play / next and the progress. Clicking the
// title or cover opens Now Playing. The shell hides the strip in the video
// player and on the sign-in page.
Item {
    id: bar

    readonly property bool shown: MusicSession.queueSize > 0 && (MusicSession.currentTrack.id || "").length > 0
        && Navigator.currentName !== "nowplaying"

    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    height: shown ? 72 : 0
    visible: shown

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.border
        }
        Rectangle {
            y: 1
            height: 2
            width: MusicSession.duration > 0 ? parent.width * Math.min(1, MusicSession.position / MusicSession.duration) : 0
            color: Theme.accent
        }
    }

    Item {
        id: info
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing
        anchors.right: controls.left
        anchors.rightMargin: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        height: 56

        Rectangle {
            id: cover
            width: 56
            height: 56
            radius: Theme.radiusSmall
            color: Theme.surfaceRaised
            Image {
                anchors.fill: parent
                asynchronous: true
                fillMode: Image.PreserveAspectCrop
                source: Library.image(Music.coverUrl(MusicSession.currentTrack.imageItemId || "",
                                                       MusicSession.currentTrack.imageTag || "", 56))
            }
        }
        Column {
            anchors.left: cover.right
            anchors.leftMargin: Theme.spacing
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            Text {
                width: parent.width
                text: MusicSession.currentTrack.title || ""
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody
                font.weight: Theme.fontWeightBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                text: MusicSession.currentTrack.artist || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
        }
        TapHandler { onTapped: Navigator.navigate("nowplaying") }
        HoverHandler { cursorShape: Qt.PointingHandCursor }
    }

    Row {
        id: controls
        anchors.right: expand.left
        anchors.verticalCenter: parent.verticalCenter
        spacing: Theme.spacingTiny

        IconButton {
            objectName: "miniPrevious"
            iconSource: MusicIcons.previous
            toolTipText: qsTr("Previous")
            onClicked: MusicSession.previous()
        }
        IconButton {
            objectName: "miniPlay"
            iconSource: MusicSession.playing ? MusicIcons.pause : Icons.play
            toolTipText: MusicSession.playing ? qsTr("Pause") : qsTr("Play")
            onClicked: MusicSession.active ? MusicSession.togglePause() : MusicSession.resume()
        }
        IconButton {
            objectName: "miniNext"
            iconSource: MusicIcons.next
            toolTipText: qsTr("Next")
            enabled: MusicSession.hasNext
            onClicked: MusicSession.next()
        }
    }
    IconButton {
        id: expand
        objectName: "miniExpand"
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSmall
        anchors.verticalCenter: parent.verticalCenter
        iconSource: MusicIcons.expand
        toolTipText: qsTr("Now playing")
        onClicked: Navigator.navigate("nowplaying")
    }
}
