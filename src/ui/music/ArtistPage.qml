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

    function retry() {
        if (item.errorText.length > 0) item.reload()
        if (list.errorText.length > 0) list.reload()
    }

    // Back from a lost connection: what failed loads again.
    Connections {
        target: Library
        function onReconnected() { page.retry() }
    }

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
                    objectName: "artistImage"
                    anchors.fill: parent
                    asynchronous: true
                    fillMode: Image.PreserveAspectCrop
                    sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
                    sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
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

        StateMessage {
            objectName: "musicError"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            visible: description.length > 0
            kind: Library.online ? "error" : "offline"
            description: page.item.errorText || page.list.errorText
            onRetry: page.retry()
        }

        SectionHeader {
            Layout.fillWidth: true
            visible: page.list.count > 0
            text: qsTr("Albums")
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            CardGrid {
                id: grid
                objectName: "musicGrid"
                anchors.fill: parent
                focus: true
                model: page.list
                cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + Theme.cardCaptionHeight
                delegate: MusicCard {
                    listModel: page.list
                    guard: guard
                }
            }
            LoadingIndicator {
                objectName: "musicLoading"
                anchors.centerIn: parent
                visible: page.list.loading && page.list.count === 0
            }
            StateMessage {
                objectName: "musicEmpty"
                anchors.centerIn: parent
                width: Math.min(parent.width, implicitWidth)
                visible: page.list.loaded && page.list.count === 0 && page.list.errorText.length === 0
                kind: "empty"
                title: qsTr("No albums by this artist.")
            }
        }
    }
}
