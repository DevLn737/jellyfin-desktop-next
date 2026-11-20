pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The floating pill at the bottom of every page with music in the queue:
// cover, title with artist and album, previous / play / next, the elapsed
// time, a seek line and the length, the volume on a wide window and Now
// Playing. Clicking the title or cover opens Now Playing too. The shell
// hides it in the video player and on the sign-in page.
Item {
    id: bar

    readonly property bool shown: MusicSession.queueSize > 0 && (MusicSession.currentTrack.id || "").length > 0
        && Navigator.currentName !== "nowplaying"

    function clock(seconds) {
        const total = Math.max(0, Math.floor(seconds || 0))
        const m = Math.floor(total / 60)
        const s = total % 60
        return m + ":" + (s < 10 ? "0" + s : s)
    }

    anchors.left: parent ? parent.left : undefined
    anchors.right: parent ? parent.right : undefined
    height: shown ? pill.height + 2 * Theme.spacingSmall : 0
    visible: shown

    Rectangle {
        id: pill
        objectName: "miniPill"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: Math.min(parent.width - 2 * Theme.spacing, 1180)
        height: 76
        radius: 20
        color: Qt.alpha(Theme.surface, 0.97)
        border.width: 1
        border.color: Theme.border

        Item {
            id: info
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingSmall + 4
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(300, pill.width * 0.3)
            height: 56

            Rectangle {
                id: cover
                width: 56
                height: 56
                radius: Theme.radius
                color: Theme.surfaceRaised
                clip: true
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
                spacing: 2
                Text {
                    width: parent.width
                    text: MusicSession.currentTrack.title || ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: [MusicSession.currentTrack.artist || "", MusicSession.currentTrack.album || ""]
                          .filter(part => part.length > 0).join(" · ")
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
            anchors.left: info.right
            anchors.leftMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            IconButton {
                objectName: "miniPrevious"
                anchors.verticalCenter: parent.verticalCenter
                iconSource: MusicIcons.previous
                toolTipText: qsTr("Previous")
                onClicked: MusicSession.previous()
            }
            // Play and pause: the round light button.
            IconButton {
                id: playButton
                objectName: "miniPlay"
                anchors.verticalCenter: parent.verticalCenter
                implicitWidth: 52
                implicitHeight: 52
                iconSource: MusicSession.playing ? MusicIcons.pause : Icons.play
                toolTipText: MusicSession.playing ? qsTr("Pause") : qsTr("Play")
                onClicked: MusicSession.active ? MusicSession.togglePause() : MusicSession.resume()
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        source: playButton.iconSource
                        color: "#0b0e15"
                    }
                }
                background: Rectangle {
                    radius: width / 2
                    color: playButton.down ? "#d8dce4" : playButton.hovered ? "#ffffff" : "#f2f4f8"
                    FocusFrame { shown: playButton.visualFocus; radius: width / 2 }
                }
            }
            IconButton {
                objectName: "miniNext"
                anchors.verticalCenter: parent.verticalCenter
                iconSource: MusicIcons.next
                toolTipText: qsTr("Next")
                enabled: MusicSession.hasNext
                onClicked: MusicSession.next()
            }
        }

        // Elapsed, the seek line and the length.
        Item {
            id: progress
            anchors.left: controls.right
            anchors.leftMargin: Theme.spacing
            anchors.right: tail.left
            anchors.rightMargin: Theme.spacing
            anchors.verticalCenter: parent.verticalCenter
            height: Theme.controlHeight
            visible: width > 120

            Text {
                id: elapsed
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: bar.clock(seek.pressed ? seek.value : MusicSession.position)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.features: { "tnum": 1 }
            }
            ValueSlider {
                id: seek
                objectName: "miniSeek"
                anchors.left: elapsed.right
                anchors.leftMargin: Theme.spacingSmall
                anchors.right: length.left
                anchors.rightMargin: Theme.spacingSmall
                anchors.verticalCenter: parent.verticalCenter
                from: 0
                to: Math.max(1, MusicSession.duration)
                focusPolicy: Qt.TabFocus
                Binding on value {
                    when: !seek.pressed
                    value: MusicSession.position
                }
                onPressedChanged: if (!pressed) MusicSession.seek(value)
                Accessible.name: qsTr("Position")
            }
            Text {
                id: length
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: bar.clock(MusicSession.duration)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                font.features: { "tnum": 1 }
            }
        }

        Row {
            id: tail
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingSmall + 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            ValueSlider {
                id: volume
                objectName: "miniVolume"
                // Out of the way on a narrow window; Now Playing has it too.
                visible: bar.width >= 960
                anchors.verticalCenter: parent.verticalCenter
                width: 96
                from: 0
                to: 100
                // The player's volume, except under the user's hand.
                Binding on value {
                    when: !volume.pressed
                    value: MusicSession.volume
                }
                onMoved: MusicSession.volume = Math.round(value)
                Accessible.name: qsTr("Volume")
            }
            IconButton {
                id: expand
                objectName: "miniExpand"
                anchors.verticalCenter: parent.verticalCenter
                iconSource: MusicIcons.expand
                toolTipText: qsTr("Now playing")
                onClicked: Navigator.navigate("nowplaying")
            }
        }
    }
}
