pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// An album or a playlist: the cover and facts, Play / Shuffle / queue
// buttons and the tracks. `trackKind` is albumTracks or playlistTracks.
RoutePage {
    id: page

    property string trackKind: "albumTracks"
    readonly property var item: Music.createItem(routeArgument, page)
    readonly property var list: {
        const model = Music.createList(trackKind, page)
        model.parentId = routeArgument
        return model
    }

    title: item.name || qsTr("Music")

    PlayGuard { id: guard }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingHuge
        anchors.rightMargin: Theme.spacingHuge
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLarge

            Rectangle {
                Layout.preferredWidth: 200
                Layout.preferredHeight: 200
                radius: Theme.radius
                color: Theme.surfaceRaised
                clip: true
                Image {
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    source: Library.image(page.item.imageUrl)
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.spacingSmall

                Heading {
                    objectName: "musicTitle"
                    Layout.fillWidth: true
                    text: page.item.name
                    wrapMode: Text.Wrap
                }
                Text {
                    id: artist
                    visible: text.length > 0
                    text: page.item.artistName
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeLarge
                    TapHandler {
                        enabled: page.item.artistId.length > 0
                        onTapped: Navigator.navigate("artist/" + page.item.artistId)
                    }
                }
                Text {
                    objectName: "musicFacts"
                    Layout.fillWidth: true
                    text: {
                        const parts = []
                        if (page.item.year)
                            parts.push(String(page.item.year))
                        if (page.list.count > 0)
                            parts.push(qsTr("%n track(s)", "", page.list.count))
                        if (page.list.totalSeconds > 0)
                            parts.push(Music.formatTotal(page.list.totalSeconds))
                        if (page.item.genres.length > 0)
                            parts.push(page.item.genres.join(", "))
                        return parts.join(" · ")
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    ActionButton {
                        objectName: "musicPlay"
                        primary: true
                        text: qsTr("Play")
                        iconSource: Icons.play
                        enabled: MusicSession.available && page.list.count > 0
                        onClicked: guard.run(() => page.list.playAll(false))
                    }
                    ActionButton {
                        objectName: "musicShuffle"
                        text: qsTr("Shuffle")
                        iconSource: MusicIcons.shuffle
                        enabled: MusicSession.available && page.list.count > 0
                        onClicked: guard.run(() => page.list.playAll(true))
                    }
                    ActionButton {
                        objectName: "musicPlayNext"
                        text: qsTr("Play next")
                        enabled: MusicSession.available && page.list.count > 0
                        onClicked: {
                            for (let i = page.list.count - 1; i >= 0; --i)
                                page.list.enqueue(i, true)
                        }
                    }
                    ActionButton {
                        objectName: "musicQueue"
                        text: qsTr("Add to queue")
                        enabled: MusicSession.available && page.list.count > 0
                        onClicked: {
                            for (let i = 0; i < page.list.count; ++i)
                                page.list.enqueue(i, false)
                        }
                    }
                    ActionButton {
                        objectName: "musicFavorite"
                        text: page.item.favorite ? qsTr("Remove from favorites") : qsTr("Favorite")
                        onClicked: page.item.toggleFavorite()
                    }
                }
                GroupHint { Layout.fillWidth: true }
            }
        }

        Text {
            objectName: "musicError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.item.errorText || page.list.errorText
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        ListView {
            id: tracks
            objectName: "musicTracks"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            model: page.list
            delegate: TrackRow {
                width: ListView.view.width
                listModel: page.list
                guard: guard
                showArtist: true
                numbered: page.trackKind === "playlistTracks"
            }
            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.list.loading && page.list.count === 0
            }
        }
    }
}
