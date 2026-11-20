pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The music library: an overview (recently added and favourites to play at
// once, recently played and artists), then albums, artists, playlists,
// genres, songs and recently played as tabs in a frame, with Shuffle all.
// Every tab is one list that loads its pages as they come into view, so a
// library of tens of thousands of albums opens at once.
RoutePage {
    id: page

    readonly property var tabs: [
        { kind: "overview", label: qsTr("Overview") },
        { kind: "albums", label: qsTr("Albums") },
        { kind: "artists", label: qsTr("Artists") },
        { kind: "playlists", label: qsTr("Playlists") },
        { kind: "genres", label: qsTr("Genres") },
        { kind: "songs", label: qsTr("Songs") },
        { kind: "recentlyPlayed", label: qsTr("Recent") },
    ]
    property int tab: 0
    readonly property string kind: tabs[tab].kind
    readonly property bool overview: kind === "overview"
    readonly property bool trackList: kind === "songs"
    // One list for every tab but the overview: a change of tab changes its kind.
    readonly property var list: Music.createList("albums", page)
    // The overview's lists, made when it first shows.
    property var recentList: null
    property var artistList: null
    property var addedList: null
    property var favouriteList: null
    function openOverview() {
        if (recentList) return
        recentList = Music.createList("recentlyPlayed", page)
        artistList = Music.createList("artists", page)
        addedList = Music.createList("albums", page)
        addedList.sortBy = "added"
        addedList.descending = true
        favouriteList = Music.createList("songs", page)
        favouriteList.favoritesOnly = true
    }
    function showTab(kind) {
        for (let i = 0; i < tabs.length; ++i)
            if (tabs[i].kind === kind) tab = i
    }
    Component.onCompleted: if (overview) openOverview()

    onKindChanged: {
        if (kind === "overview") {
            openOverview()
            return
        }
        list.sortBy = "name"
        list.descending = false
        list.searchTerm = ""
        search.text = ""
        list.kind = kind
    }

    title: qsTr("Music")
    // Every song, for Shuffle all from any tab (made at the first use).
    property var allSongs: null

    PlayGuard { id: guard }

    // Back from a lost connection: a list that failed loads again.
    Connections {
        target: Library
        function onReconnected() { if (page.list.errorText.length > 0) page.list.reload() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        // The tabs in a frame, and Shuffle all.
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing

            Rectangle {
                id: tabsFrame
                objectName: "musicTabsFrame"
                readonly property int padding: 5
                Layout.preferredWidth: Math.min(tabRow.implicitWidth + 2 * padding, page.width - 2 * Theme.pageMargin - shuffleAll.width - Theme.spacing)
                Layout.preferredHeight: Theme.controlHeight + 4 + 2 * padding
                radius: Theme.radiusLarge + 2
                color: Qt.alpha(Theme.text, 0.04)
                border.width: 1
                border.color: Theme.glassBorder
                clip: true

                Flickable {
                    anchors.fill: parent
                    anchors.margins: tabsFrame.padding
                    contentWidth: tabRow.implicitWidth
                    flickableDirection: Flickable.HorizontalFlick
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: tabRow
                        height: parent.height
                        spacing: 2
                        Repeater {
                            model: page.tabs
                            delegate: T.ItemDelegate {
                                id: tabButton
                                required property var modelData
                                required property int index
                                readonly property bool current: page.tab === index
                                objectName: "musicTab_" + modelData.kind
                                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                                height: tabRow.height
                                leftPadding: 14
                                rightPadding: 14
                                focusPolicy: Qt.StrongFocus
                                hoverEnabled: true
                                Accessible.name: modelData.label
                                Accessible.role: Accessible.PageTab
                                Keys.onReturnPressed: tabButton.clicked()
                                Keys.onEnterPressed: tabButton.clicked()
                                onClicked: page.tab = index
                                contentItem: Row {
                                    spacing: Theme.spacingSmall
                                    Text {
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: tabButton.modelData.label
                                        color: tabButton.current || tabButton.hovered ? Theme.text : Theme.textSecondary
                                        font.pixelSize: Theme.fontSizeBody
                                        font.weight: Font.Bold
                                    }
                                    // The open tab's count.
                                    Text {
                                        objectName: tabButton.current ? "musicCount" : ""
                                        anchors.verticalCenter: parent.verticalCenter
                                        visible: tabButton.current && !page.overview && page.list.errorText.length === 0
                                                 && !(page.list.loading && page.list.count === 0)
                                        text: String(page.list.count)
                                        color: Theme.textDisabled
                                        font.pixelSize: Theme.fontSizeSmall
                                        font.weight: Font.Bold
                                    }
                                }
                                background: Rectangle {
                                    radius: Theme.radius + 3
                                    color: tabButton.current ? Qt.alpha(Theme.text, 0.12)
                                         : tabButton.hovered ? Qt.alpha(Theme.text, 0.05) : "transparent"
                                    FocusFrame { shown: tabButton.visualFocus; inset: 0 }
                                }
                            }
                        }
                    }
                }
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                id: shuffleAll
                objectName: "musicShuffle"
                text: qsTr("Shuffle all")
                iconSource: MusicIcons.shuffle
                enabled: MusicSession.available
                onClicked: guard.run(() => {
                    if (page.trackList) page.list.playAll(true)
                    else {
                        if (!page.allSongs) page.allSongs = Music.createList("songs", page)
                        page.allSongs.playAll(true)
                    }
                })
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
            GroupHint { Layout.fillWidth: true }
        }

        StateMessage {
            objectName: "musicError"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            visible: !page.overview && page.list.errorText.length > 0
            kind: Library.online ? "error" : "offline"
            description: page.list.errorText
            onRetry: page.list.reload()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            CardGrid {
                id: grid
                objectName: "musicGrid"
                anchors.fill: parent
                visible: !page.trackList && !page.overview
                focus: !page.trackList && !page.overview
                model: page.trackList || page.overview ? null : page.list
                cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + Theme.cardCaptionHeight
                delegate: MusicCard {
                    listModel: page.list
                    guard: guard
                }
            }

            ListView {
                SmoothWheel {}
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

            // The overview: two cards to play at once, then rows.
            Flickable {
                id: overviewPane
                objectName: "musicOverview"
                anchors.fill: parent
                visible: page.overview
                focus: page.overview
                contentHeight: overviewColumn.implicitHeight + Theme.spacingLarge
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                SmoothWheel {}

                Column {
                    id: overviewColumn
                    width: parent.width
                    spacing: Theme.spacingLarge

                    Row {
                        id: features
                        width: parent.width
                        spacing: Theme.spacing
                        readonly property real cardWidth: (width - spacing) / 2

                        FeatureCard {
                            objectName: "musicRecentlyAdded"
                            width: features.cardWidth
                            title: qsTr("Recently added")
                            subtitle: page.addedList ? qsTr("%n album(s)", "", page.addedList.count) : ""
                            onOpened: {
                                page.showTab("albums")
                                page.list.sortBy = "added"
                                page.list.descending = true
                            }
                            onPlayed: guard.run(() => page.addedList.play(0))
                        }
                        FeatureCard {
                            objectName: "musicFavourites"
                            width: features.cardWidth
                            title: qsTr("Favourites")
                            subtitle: page.favouriteList ? qsTr("%n track(s)", "", page.favouriteList.count) : ""
                            enabled: page.favouriteList !== null && page.favouriteList.count > 0
                            onOpened: page.favouriteList.playAll(false)
                            onPlayed: guard.run(() => page.favouriteList.playAll(false))
                        }
                    }

                    CardRow {
                        objectName: "musicRecentRow"
                        width: parent.width
                        visible: count > 0
                        title: qsTr("Recently played")
                        model: page.overview ? page.recentList : null
                        cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + Theme.cardCaptionHeight
                        canShowAll: true
                        onShowAllClicked: page.showTab("recentlyPlayed")
                        delegate: MusicCard {
                            listModel: page.recentList
                            guard: guard
                        }
                    }
                    CardRow {
                        objectName: "musicArtistRow"
                        width: parent.width
                        visible: count > 0
                        title: qsTr("Artists")
                        model: page.overview ? page.artistList : null
                        cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + Theme.cardCaptionHeight
                        canShowAll: true
                        onShowAllClicked: page.showTab("artists")
                        delegate: MusicCard {
                            listModel: page.artistList
                            guard: guard
                        }
                    }
                }
            }

            LoadingIndicator {
                anchors.centerIn: parent
                visible: !page.overview && page.list.loading && page.list.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: !page.overview && !page.list.loading && page.list.count === 0 && page.list.errorText.length === 0
                text: qsTr("Nothing here yet.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }

    // A wide card of the overview: the first cover of its list behind a
    // shade, the title, a line under it and a round play button.
    component FeatureCard: T.ItemDelegate {
        id: card
        property string title
        property string subtitle
        signal opened()
        signal played()
        height: 150
        padding: 0
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.name: title
        Keys.onReturnPressed: card.played()
        Keys.onEnterPressed: card.played()
        onClicked: card.opened()

        background: Rectangle {
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            clip: true
            // A soft glow of the accent from the right.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: Qt.alpha(Theme.accent, 0.04) }
                    GradientStop { position: 1; color: Qt.alpha(Theme.accent, card.hovered ? 0.30 : 0.22) }
                }
            }
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: card.visualFocus ? 3 : 1
                border.color: card.visualFocus ? Theme.focusRing : Theme.glassBorder
            }
        }
        contentItem: Item {
            Column {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLarge
                anchors.right: playRound.left
                anchors.rightMargin: Theme.spacing
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacingLarge
                spacing: 2
                Text {
                    width: parent.width
                    text: card.title
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeTitle
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    visible: text.length > 0
                    text: card.subtitle
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.Bold
                }
            }
            IconButton {
                id: playRound
                objectName: card.objectName + "Play"
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLarge
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacingLarge
                implicitWidth: 52
                implicitHeight: 52
                enabled: card.enabled
                iconSource: Icons.play
                toolTipText: qsTr("Play")
                onClicked: card.played()
                contentItem: Item {
                    Icon {
                        anchors.centerIn: parent
                        source: Icons.play
                        color: "#0b0e15"
                    }
                }
                background: Rectangle {
                    radius: width / 2
                    color: playRound.hovered ? "#ffffff" : "#f2f4f8"
                    opacity: playRound.enabled ? 1 : 0.4
                }
            }
        }
    }
}
