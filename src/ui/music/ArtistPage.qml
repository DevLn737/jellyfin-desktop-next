pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// An artist: route "artist/<id>". The portrait, Shuffle / Play buttons and
// the artist's albums.
RoutePage {
    id: page

    readonly property var item: Music.createItem(routeArgument, page)
    readonly property var list: {
        const model = Music.createList("albums", page)
        model.artistId = routeArgument
        model.sortBy = "year"
        model.descending = true
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
                Layout.preferredWidth: 160
                Layout.preferredHeight: 160
                radius: 80
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
                    Layout.fillWidth: true
                    text: page.item.genres.join(", ")
                    visible: text.length > 0
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    wrapMode: Text.Wrap
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSmall
                    ActionButton {
                        objectName: "musicShuffle"
                        primary: true
                        text: qsTr("Shuffle")
                        iconSource: MusicIcons.shuffle
                        enabled: MusicSession.available
                        onClicked: guard.run(() => MusicSession.playArtist(page.routeArgument, true))
                    }
                    ActionButton {
                        objectName: "musicPlay"
                        text: qsTr("Play")
                        iconSource: Icons.play
                        enabled: MusicSession.available
                        onClicked: guard.run(() => MusicSession.playArtist(page.routeArgument, false))
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

        SectionHeader {
            Layout.fillWidth: true
            text: qsTr("Albums")
        }

        CardGrid {
            id: grid
            objectName: "musicGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            model: page.list
            cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + 52
            delegate: MusicCard {
                listModel: page.list
                guard: guard
            }
        }
    }
}
