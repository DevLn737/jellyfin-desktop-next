pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The music library: recently played, albums, artists, playlists, genres
// and songs. Every tab is one list that loads its pages as they come into
// view, so a library of tens of thousands of albums opens at once.
RoutePage {
    id: page

    readonly property var tabs: [
        { kind: "recentlyPlayed", label: qsTr("Recent") },
        { kind: "albums", label: qsTr("Albums") },
        { kind: "artists", label: qsTr("Artists") },
        { kind: "playlists", label: qsTr("Playlists") },
        { kind: "genres", label: qsTr("Genres") },
        { kind: "songs", label: qsTr("Songs") },
    ]
    property int tab: 1
    readonly property string kind: tabs[tab].kind
    readonly property bool trackList: kind === "songs"
    // One list for every tab: a change of tab changes its kind.
    readonly property var list: Music.createList("albums", page)

    onKindChanged: {
        list.sortBy = "name"
        list.descending = false
        list.searchTerm = ""
        search.text = ""
        list.kind = kind
    }

    title: qsTr("Music")

    PlayGuard { id: guard }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingHuge
        anchors.rightMargin: Theme.spacingHuge
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            Heading {
                Layout.fillWidth: true
                text: page.title
            }
            Text {
                objectName: "musicCount"
                text: qsTr("%n item(s)", "", page.list.count)
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: Theme.spacingSmall
            Repeater {
                model: page.tabs
                delegate: ActionButton {
                    required property var modelData
                    required property int index
                    objectName: "musicTab_" + modelData.kind
                    text: modelData.label
                    primary: page.tab === index
                    onClicked: page.tab = index
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            visible: page.kind === "albums" || page.kind === "songs" || page.kind === "artists"

            InputField {
                id: search
                objectName: "musicSearch"
                Layout.preferredWidth: 280
                placeholderText: qsTr("Search")
                onTextChanged: page.list.searchTerm = text
            }
            ChoiceBox {
                objectName: "musicSort"
                implicitWidth: 200
                model: [
                    { value: "name", label: qsTr("Name") },
                    { value: "added", label: qsTr("Date added") },
                    { value: "year", label: qsTr("Year") },
                    { value: "plays", label: qsTr("Most played") },
                    { value: "random", label: qsTr("Random") },
                ]
                currentIndex: Math.max(0, indexOfValue(page.list.sortBy))
                onActivated: {
                    page.list.sortBy = currentValue
                    page.list.descending = currentValue === "added" || currentValue === "plays"
                }
            }
            ActionButton {
                objectName: "musicShuffle"
                visible: page.kind === "songs"
                text: qsTr("Shuffle all")
                iconSource: MusicIcons.shuffle
                enabled: MusicSession.available
                onClicked: guard.run(() => page.list.playAll(true))
            }
            GroupHint { Layout.fillWidth: true }
        }

        Text {
            objectName: "musicError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.list.errorText
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            CardGrid {
                id: grid
                objectName: "musicGrid"
                anchors.fill: parent
                visible: !page.trackList
                focus: !page.trackList
                model: page.trackList ? null : page.list
                cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + 52
                delegate: MusicCard {
                    listModel: page.list
                    guard: guard
                }
            }

            ListView {
                id: songs
                objectName: "musicSongs"
                anchors.fill: parent
                visible: page.trackList
                focus: page.trackList
                clip: true
                keyNavigationEnabled: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.trackList ? page.list : null
                delegate: TrackRow {
                    width: ListView.view.width
                    listModel: page.list
                    guard: guard
                    showAlbum: true
                    numbered: true
                }
            }

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.list.loading && page.list.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: !page.list.loading && page.list.count === 0 && page.list.errorText.length === 0
                text: qsTr("Nothing here yet.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }
}
