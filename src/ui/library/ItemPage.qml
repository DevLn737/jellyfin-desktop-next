pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// A movie, series, season or episode: the backdrop across the window's top
// (under the navigation bar), fading into the page on the left and at the
// bottom; over it the series (for a season or episode) or the kind, the logo
// or the title, genres, year, seasons, episodes, length, age rating, score,
// quality and studio, the description, then Play or Resume, From the
// beginning, favorite and watched, Download and a menu (Refresh, Go to
// series). The episode Play starts shows under the buttons, then a movie's
// or series' pages on other sites (Shikimori, IMDb, ...), which open in the
// browser.
//
// A series or season then lists its episodes in a grid, four across on a
// wide page, by season (tabs named "Season N" from the season's number,
// whatever the server calls it, with the number of episodes); a season of
// more than 24 episodes is split into ranges (1–24, 25–48, ...; a last few
// episodes join the range before) and opens at the range of the next
// episode to watch. Then the cast, the details
// (studio, genres, audio, subtitles, quality), the collections and similar
// items.
RoutePage {
    id: page

    property ItemDetails details: null
    readonly property bool loaded: details !== null && details.loaded
    readonly property bool hasEpisodes: details && (details.type === "Series" || details.type === "Season")
    readonly property bool isSeries: details && details.type === "Series"
    readonly property bool isEpisode: details && details.type === "Episode"
    readonly property bool inSeries: details !== null && details.seriesId.length > 0
                                     && (details.type === "Season" || details.type === "Episode")
    // Episodes per range of a long season; a last range of this many or
    // fewer joins the one before (1–24, 25–49 rather than 49–49).
    readonly property int rangeSize: 24
    readonly property int rangeTail: 6

    // How many ranges a season of `total` episodes has, and where one
    // starts and ends (0-based, end exclusive).
    function rangeCountFor(total) {
        const n = Math.ceil(total / rangeSize)
        return n > 1 && total - (n - 1) * rangeSize <= rangeTail ? n - 1 : n
    }
    function rangeStart(index) {
        return index * rangeSize
    }
    function rangeEnd(index, total) {
        return index >= rangeCountFor(total) - 1 ? total : Math.min(total, (index + 1) * rangeSize)
    }

    // A season's tab: "Season N" from its number, the same for every
    // series however the server names its seasons ("Сезон 1", "Season 2",
    // "Kingdom Season 3", "[ТВ-2]"); season 0 is the specials. A season
    // without a number keeps its own name.
    function seasonLabel(indexNumber, name) {
        if (indexNumber === undefined || indexNumber === null || indexNumber < 0)
            return name
        return indexNumber === 0 ? qsTr("Specials") : qsTr("Season %1").arg(indexNumber)
    }

    // A season by its number, as its tab on the series page.
    readonly property string displayName: !loaded ? ""
        : details.type === "Season" ? seasonLabel(details.indexNumber, details.name) : details.name
    title: displayName
    hasTopBar: true
    underTopBar: true
    topBarTransparent: flick.contentY < Theme.spacingLarge

    Component.onCompleted: details = Library.createDetails(routeArgument, page)

    // Back from a lost connection: a page that failed loads again.
    Connections {
        target: Library
        function onReconnected() {
            if (page.details && !page.details.loaded && page.details.errorText.length > 0) page.details.reload()
        }
    }

    function clock(ticks) {
        const total = Math.floor(ticks / 10000000)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const pad = (n) => n < 10 ? "0" + n : "" + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    function kindName(type) {
        switch (type) {
        case "Movie": return qsTr("Film")
        case "Series": return qsTr("Series")
        case "Season": return qsTr("Season")
        case "Episode": return qsTr("Episode")
        default: return ""
        }
    }

    // A crew member's job, which the server names in English.
    function roleName(role) {
        switch (role) {
        case "Director": return qsTr("Director")
        case "Writer": return qsTr("Writer")
        case "Producer": return qsTr("Producer")
        case "Composer": return qsTr("Composer")
        default: return role
        }
    }

    LoadingIndicator {
        anchors.centerIn: parent
        visible: page.details && page.details.loading && !page.details.loaded
    }

    StateMessage {
        objectName: "itemError"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.pageMargin, 600)
        visible: page.details && !page.details.loaded && page.details.errorText.length > 0
        kind: Library.online ? "error" : "offline"
        description: page.details ? page.details.errorText : ""
        onRetry: if (page.details) page.details.reload()
    }

    // The backdrop, fixed to the window's top (outside the Flickable, so the
    // content scrolls over it). A scrim over it grows with the scrolling.
    Item {
        id: backdrop
        objectName: "itemBackdrop"
        z: -1
        width: page.width
        height: Math.max(Math.min(page.height * 0.92, width * 9 / 16), Math.min(page.height, 520))
        visible: page.loaded
        readonly property color pageColor: Theme.background
        readonly property real scrolled: Math.max(0, Math.min(1, flick.contentY / (height * 0.6)))
        Image {
            anchors.fill: parent
            source: page.details ? Library.image(page.details.backdropUrl) : ""
            sourceSize.width: width
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignTop
            asynchronous: true
            opacity: status === Image.Ready ? 0.9 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
        }
        // Into the page colour on the left, under the text...
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Qt.alpha(backdrop.pageColor, 0.92) }
                GradientStop { position: 0.3; color: Qt.alpha(backdrop.pageColor, 0.6) }
                GradientStop { position: 0.65; color: Qt.alpha(backdrop.pageColor, 0.1) }
                GradientStop { position: 1.0; color: Qt.alpha(backdrop.pageColor, 0.0) }
            }
        }
        // ... at the top, under the bar, and at the bottom.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.alpha(backdrop.pageColor, 0.5) }
                GradientStop { position: 0.18; color: Qt.alpha(backdrop.pageColor, 0.0) }
                GradientStop { position: 0.55; color: Qt.alpha(backdrop.pageColor, 0.1) }
                GradientStop { position: 0.85; color: Qt.alpha(backdrop.pageColor, 0.8) }
                GradientStop { position: 1.0; color: backdrop.pageColor }
            }
        }
        Rectangle {
            objectName: "itemBackdropScrim"
            anchors.fill: parent
            color: backdrop.pageColor
            opacity: backdrop.scrolled * 0.75
        }
    }

    Flickable {
        SmoothWheel {}
        id: flick
        objectName: "itemFlick"
        anchors.fill: parent
        visible: page.loaded
        contentHeight: main.y + main.implicitHeight + 2 * Theme.spacingHuge
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // --- The information over the backdrop ----------------------------
        Column {
            id: info
            x: Theme.pageMargin
            // Its bottom near the backdrop's, below the bar.
            y: Math.max(page.topInset + Theme.spacingHuge, Math.round(backdrop.height * 0.86) - implicitHeight)
            width: Math.min(flick.width - 2 * Theme.pageMargin, Theme.tvMode ? 1200 : 860)
            spacing: Theme.spacing

            // The series of a season or episode (it leads there), or the
            // kind.
            Text {
                objectName: "itemSeries"
                width: parent.width
                visible: page.inSeries
                text: page.details ? (page.details.seriesName + (page.details.subtitle.length > 0 ? " · " + page.details.subtitle : "")).toUpperCase() : ""
                color: Theme.accent
                font.pixelSize: Theme.fontSizeSmall + 1
                font.weight: Font.ExtraBold
                font.letterSpacing: 1.6
                elide: Text.ElideRight
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: Navigator.navigate(Library.routeFor(page.details.seriesId, "Series"))
                }
            }
            Text {
                objectName: "itemKind"
                width: parent.width
                visible: !page.inSeries && text.length > 0
                text: page.details ? page.kindName(page.details.type).toUpperCase() : ""
                color: Theme.accent
                font.pixelSize: Theme.fontSizeSmall + 1
                font.weight: Font.ExtraBold
                font.letterSpacing: 1.6
                elide: Text.ElideRight
            }

            TitleLogo {
                objectName: "itemName"
                width: parent.width
                text: page.displayName
                // A season or episode has its own name, not the series' logo.
                source: page.details && !page.isEpisode && page.details.type !== "Season"
                        && String(page.details.logoUrl).length > 0 ? Library.image(page.details.logoUrl) : ""
                maxWidth: Math.min(parent.width, Theme.tvMode ? 760 : 600)
                maxHeight: Theme.tvMode ? 220 : 180
                fontSize: Theme.compact ? Theme.fontSizeHeadline + 8 : Theme.tvMode ? 88 : 68
                fontWeight: Font.Black
                maximumLineCount: 3
            }

            Flow {
                objectName: "itemGenres"
                width: parent.width
                spacing: Theme.spacingSmall
                visible: page.details && page.details.genres.length > 0
                Repeater {
                    model: page.details ? page.details.genres.slice(0, 4) : []
                    Chip { required property string modelData; text: modelData }
                }
            }

            // Year, seasons, episodes, length, age rating, score, quality,
            // studio.
            Flow {
                objectName: "itemMeta"
                width: parent.width
                spacing: Theme.spacingLarge
                readonly property var parts: {
                    const d = page.details
                    if (!d)
                        return []
                    const out = []
                    if (d.year) out.push(String(d.year))
                    if (page.isSeries && d.seasons.count > 0) out.push(qsTr("%n season(s)", "", d.seasons.count))
                    if (page.hasEpisodes && d.episodeCount > 0) out.push(qsTr("%n episode(s)", "", d.episodeCount))
                    const runTime = Library.formatRunTime(d.runTimeTicks)
                    if (runTime.length > 0 && !page.hasEpisodes) out.push(runTime)
                    if (d.officialRating.length > 0) out.push(d.officialRating)
                    return out
                }
                Repeater {
                    model: parent.parts
                    Text {
                        required property string modelData
                        height: qualityChip.height
                        verticalAlignment: Text.AlignVCenter
                        text: modelData
                        color: Theme.text
                        opacity: 0.88
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                    }
                }
                Row {
                    objectName: "itemRating"
                    visible: page.details && !!page.details.communityRating
                    height: qualityChip.height
                    spacing: 4
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "★"
                        color: "#f5c04a"
                        font.pixelSize: Theme.fontSizeLarge
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.details && page.details.communityRating
                            ? Number(page.details.communityRating).toLocaleString(Qt.locale(), "f", 1) : ""
                        color: Theme.text
                        opacity: 0.88
                        font.pixelSize: Theme.fontSizeLarge
                        font.weight: Font.DemiBold
                    }
                }
                Rectangle {
                    id: qualityChip
                    objectName: "itemQuality"
                    visible: page.details && page.details.quality.length > 0
                    width: qualityText.implicitWidth + 14
                    height: qualityText.implicitHeight + 8
                    radius: Theme.radiusSmall + 1
                    color: "transparent"
                    border.width: 1
                    border.color: Qt.alpha(Theme.text, 0.4)
                    Text {
                        id: qualityText
                        anchors.centerIn: parent
                        text: page.details ? page.details.quality : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Font.ExtraBold
                    }
                }
                Text {
                    visible: page.details && page.details.studios.length > 0
                    height: qualityChip.height
                    verticalAlignment: Text.AlignVCenter
                    text: page.details && page.details.studios.length > 0 ? page.details.studios[0] : ""
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeLarge
                }
            }

            Text {
                id: overview
                objectName: "itemOverview"
                property bool expanded: false
                width: Math.min(parent.width, Theme.tvMode ? 1100 : 760)
                visible: text.length > 0
                text: page.details ? page.details.overview : ""
                color: Theme.text
                opacity: 0.8
                font.pixelSize: Theme.fontSizeBody + 1
                lineHeight: 1.35
                wrapMode: Text.Wrap
                maximumLineCount: expanded ? 100 : 3
                elide: Text.ElideRight
            }
            LinkButton {
                objectName: "overviewMore"
                visible: overview.truncated || overview.expanded
                text: overview.expanded ? qsTr("Less") : qsTr("More")
                onClicked: overview.expanded = !overview.expanded
            }

            Item { width: 1; height: Theme.spacingTiny }

            Flow {
                width: parent.width
                spacing: Theme.spacing
                readonly property int buttonHeight: Theme.tvMode ? 72 : Theme.compact ? Theme.controlHeight : 48

                ActionButton {
                    id: playButton
                    objectName: "playButton"
                    primary: true
                    focus: true
                    implicitHeight: parent.buttonHeight
                    leftPadding: Theme.spacingHuge - 4
                    rightPadding: Theme.spacingHuge - 4
                    font.pixelSize: Theme.fontSizeLarge - 1
                    iconSource: Icons.play
                    visible: page.details && page.details.playTargetId.length > 0
                    progress: page.details && page.details.resumePositionTicks > 0 ? page.details.playTargetProgress : -1
                    text: page.details && page.details.resumePositionTicks > 0
                        ? qsTr("Resume from %1").arg(page.clock(page.details.resumePositionTicks))
                        : qsTr("Play")
                    onClicked: page.details.resumePositionTicks > 0 ? page.details.resume() : page.details.play()
                }
                ActionButton {
                    objectName: "playFromStartButton"
                    implicitHeight: parent.buttonHeight
                    font.pixelSize: Theme.fontSizeLarge - 1
                    visible: page.details && page.details.resumePositionTicks > 0
                    text: qsTr("From the beginning")
                    onClicked: page.details.play()
                }
                IconButton {
                    objectName: "favoriteButton"
                    square: true
                    implicitWidth: parent.buttonHeight
                    implicitHeight: parent.buttonHeight
                    iconSource: page.details !== null && page.details.favorite ? Icons.favorite : Icons.favoriteBorder
                    toolTipText: page.details !== null && page.details.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                    onClicked: page.details.toggleFavorite()
                }
                IconButton {
                    objectName: "playedButton"
                    square: true
                    implicitWidth: parent.buttonHeight
                    implicitHeight: parent.buttonHeight
                    iconSource: Icons.check
                    opacity: page.details !== null && page.details.played ? 1 : 0.75
                    toolTipText: page.details !== null && page.details.played ? qsTr("Watched") : qsTr("Mark watched")
                    onClicked: page.details.togglePlayed()
                }
                DownloadActions {
                    objectName: "downloadActions"
                    buttonHeight: parent.buttonHeight
                    buttonFontSize: Theme.fontSizeLarge - 1
                    itemId: page.details ? page.details.itemId : ""
                    itemType: page.loaded ? page.details.type : ""
                    itemName: page.title
                }
                IconButton {
                    id: moreButton
                    objectName: "itemMoreButton"
                    square: true
                    implicitWidth: parent.buttonHeight
                    implicitHeight: parent.buttonHeight
                    iconSource: Icons.more
                    toolTipText: qsTr("More actions")
                    toolTipSuppressed: moreMenu.visible
                    onClicked: moreMenu.open()
                    PopupMenu {
                        id: moreMenu
                        objectName: "itemMoreMenu"
                        anchorItem: moreButton
                        entries: page.inSeries ? [qsTr("Refresh"), qsTr("Go to series")] : [qsTr("Refresh")]
                        onActivated: (index) => {
                            if (index === 0)
                                page.details.reload()
                            else
                                Navigator.navigate(Library.routeFor(page.details.seriesId, "Series"))
                        }
                    }
                }
            }

            // The episode Play starts (a series), with the time left.
            Row {
                id: targetLine
                visible: playTarget.text.length > 0
                spacing: Theme.spacing
                readonly property real progress: page.details ? page.details.playTargetProgress : 0
                Text {
                    id: playTarget
                    objectName: "playTarget"
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.min(implicitWidth, info.width * 0.7)
                    text: {
                        const d = page.details
                        if (!d || d.type !== "Series" || d.playTargetName.length === 0)
                            return ""
                        const what = d.playTargetSubtitle.length > 0
                            ? d.playTargetSubtitle + " «" + d.playTargetName + "»" : d.playTargetName
                        return targetLine.progress > 0 ? what : qsTr("Next: %1").arg(what)
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall + 1
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    visible: targetLine.progress > 0
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        const d = page.details
                        if (!d)
                            return ""
                        const left = Math.max(0, d.playTargetRunTimeTicks - d.resumePositionTicks)
                        return qsTr("%n min left", "", Math.max(1, Math.round(left / 600000000)))
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall + 1
                }
            }

            // The film or series on other sites, in the browser.
            Flow {
                objectName: "externalLinks"
                width: parent.width
                spacing: Theme.spacingSmall
                visible: linkRepeater.count > 0
                Repeater {
                    id: linkRepeater
                    model: page.details ? page.details.externalLinks : []
                    ExternalLinkButton {}
                }
            }
        }

        // --- Below the backdrop --------------------------------------------
        Column {
            id: main
            x: Theme.pageMargin
            y: info.y + info.height + Theme.spacingHuge + Theme.spacing
            width: flick.width - 2 * Theme.pageMargin
            spacing: Theme.spacingHuge + Theme.spacing

            // Seasons and episodes.
            Column {
                id: episodesSection
                objectName: "episodeList"
                visible: page.hasEpisodes
                width: parent.width
                spacing: Theme.spacingLarge

                // How many episodes of the season shown, and how many are
                // watched.
                readonly property var season: page.isSeries ? seasons.currentItem : null
                readonly property int total: episodes.total
                readonly property int unwatched: total - episodes.watched
                readonly property string seasonId: page.isSeries ? (page.details ? page.details.seasonId : "")
                                                                 : (page.details ? page.details.itemId : "")

                Item {
                    width: parent.width
                    height: Math.max(seasonsFrame.visible ? seasonsFrame.height : 0, markSeason.height)

                    // Seasons as tabs, with their numbers of episodes.
                    Rectangle {
                        id: seasonsFrame
                        objectName: "seasonTabsFrame"
                        readonly property int padding: 5
                        visible: page.isSeries && seasons.count > 0
                        width: seasons.width + 2 * padding
                        height: seasons.height + 2 * padding
                        radius: Theme.radiusLarge + 2
                        color: Qt.alpha(Theme.text, 0.04)
                        border.width: 1
                        border.color: Theme.glassBorder

                        ListView {
                            id: seasons
                            objectName: "seasonTabs"
                            // Up or Down into the tabs lands on the chosen
                            // season (SpatialNavigation).
                            readonly property bool enterAtCurrent: true
                            x: seasonsFrame.padding
                            y: seasonsFrame.padding
                            width: Math.min(contentWidth, episodesSection.width - watchedRow.width - Theme.spacingHuge)
                            height: Theme.controlHeight + 4
                            orientation: ListView.Horizontal
                            spacing: 2
                            clip: true
                            keyNavigationEnabled: true
                            activeFocusOnTab: true
                            highlightFollowsCurrentItem: false
                            boundsBehavior: Flickable.StopAtBounds
                            model: page.details ? page.details.seasons : null
                            currentIndex: page.details && page.details.seasons ? Math.max(0, page.details.seasons.rowOf(page.details.seasonId)) : 0
                            delegate: SeasonTab {
                                objectName: "seasonTab_" + index
                                selected: page.details !== null && page.details.seasonId === itemId
                                onClicked: page.details.seasonId = itemId
                            }
                        }
                    }

                    Row {
                        id: watchedRow
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing
                        Text {
                            objectName: "seasonWatched"
                            anchors.verticalCenter: parent.verticalCenter
                            visible: episodesSection.total > 0
                            text: qsTr("%1 of %2 watched").arg(Math.max(0, episodesSection.total - episodesSection.unwatched)).arg(episodesSection.total)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Font.DemiBold
                        }
                        ActionButton {
                            id: markSeason
                            objectName: "markSeasonButton"
                            visible: episodesSection.seasonId.length > 0 && episodesSection.total > 0
                            readonly property bool allWatched: episodesSection.unwatched === 0
                            iconSource: Icons.check
                            text: allWatched ? qsTr("Mark season unwatched") : qsTr("Mark season watched")
                            onClicked: {
                                const favorite = episodesSection.season ? episodesSection.season.favorite === true : false
                                Library.setPlayed(episodesSection.seasonId, !allWatched, favorite)
                                refreshSoon.restart()
                            }
                        }
                    }
                    // The server marks the episodes; the page shows them after.
                    Timer {
                        id: refreshSoon
                        interval: 1200
                        onTriggered: if (page.details) page.details.reload()
                    }
                }

                // A long season in ranges of episodes.
                Row {
                    objectName: "episodeRanges"
                    visible: episodes.rangeCount > 1
                    spacing: Theme.spacingSmall
                    Repeater {
                        model: episodes.rangeCount
                        T.AbstractButton {
                            id: rangeTab
                            required property int index
                            readonly property bool selected: index === episodes.range
                            objectName: "episodeRange_" + index
                            implicitWidth: rangeText.implicitWidth + 28
                            implicitHeight: Theme.tvMode ? 44 : 32
                            focusPolicy: Qt.StrongFocus
                            hoverEnabled: true
                            Accessible.role: Accessible.PageTab
                            Accessible.name: rangeText.text
                            Keys.onReturnPressed: rangeTab.clicked()
                            Keys.onEnterPressed: rangeTab.clicked()
                            onClicked: episodes.range = index
                            contentItem: Text {
                                id: rangeText
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                                text: (page.rangeStart(rangeTab.index) + 1) + "–" + page.rangeEnd(rangeTab.index, episodes.total)
                                color: rangeTab.selected || rangeTab.hovered ? Theme.text : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeSmall + 1
                                font.weight: Font.Bold
                            }
                            background: Rectangle {
                                radius: Theme.radius
                                color: rangeTab.selected ? Qt.alpha(Theme.text, 0.12) : rangeTab.hovered ? Qt.alpha(Theme.text, 0.05) : "transparent"
                                border.width: 1
                                border.color: Theme.glassBorder
                                FocusFrame { shown: rangeTab.visualFocus; inset: 0 }
                            }
                        }
                    }
                }

                // The episodes, four across on a wide page, growing down.
                Grid {
                    id: episodeGrid
                    width: parent.width
                    columns: width >= 1100 ? 4 : width >= 760 ? 3 : 2
                    columnSpacing: Theme.spacingLarge
                    rowSpacing: Theme.spacingHuge
                    readonly property real cardWidth: Math.floor((width - (columns - 1) * columnSpacing) / columns)

                    Repeater {
                        id: episodes
                        objectName: "episodes"
                        // The whole season (the grid shows one range of it).
                        readonly property var all: page.details ? page.details.episodes : null
                        readonly property int total: all ? all.count : 0
                        // The range shown of a long season.
                        property int range: 0
                        // How many are watched, kept up by the model.
                        readonly property int watched: all ? all.playedCount : 0
                        readonly property int rangeCount: page.rangeCountFor(total)
                        // Only the range shown has cards (and loads stills):
                        // a season of 600 episodes makes 24.
                        model: ItemRangeModel {
                            sourceModel: episodes.all
                            offset: page.rangeStart(episodes.range)
                            limit: page.rangeEnd(episodes.range, episodes.total) - offset
                        }
                        function openAtNext() {
                            const at = all ? all.firstUnplayedRow() : 0
                            range = Math.max(0, Math.min(rangeCount - 1, Math.floor(at / page.rangeSize)))
                        }
                        EpisodeCard {
                            cardWidth: episodeGrid.cardWidth
                            firstRow: episodes.range * page.rangeSize
                            onPlayClicked: page.details.playEpisode(episodes.range * page.rangeSize + index)
                        }
                    }
                    // A new season opens at the range of its first episode
                    // not watched.
                    Connections {
                        target: episodes.all
                        function onCountChanged() { Qt.callLater(episodes.openAtNext) }
                    }
                }
            }

            // Cast and crew.
            Column {
                objectName: "castSection"
                width: parent.width
                spacing: Theme.spacing
                visible: cast.count > 0

                SectionHeader { text: qsTr("Cast and crew") }

                ListView {
                    id: cast
                    objectName: "castRow"
                    width: parent.width
                    height: Theme.tvMode ? 250 : 180
                    orientation: ListView.Horizontal
                    spacing: Theme.spacingLarge
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    model: page.details ? page.details.people.slice(0, 30) : []
                    delegate: PersonChip {
                        onClicked: if (modelData.id.length > 0) Navigator.navigate(Library.routeFor(modelData.id, "Person"))
                    }
                }
            }

            // Studio, genres, audio, subtitles, quality.
            Column {
                id: detailsTable
                objectName: "detailsSection"
                width: parent.width
                spacing: Theme.spacing
                readonly property var rows: {
                    const d = page.details
                    if (!d)
                        return []
                    const out = []
                    if (d.studios.length > 0) out.push({ label: qsTr("Studio"), value: d.studios.slice(0, 3).join(", ") })
                    if (d.genres.length > 0) out.push({ label: qsTr("Genres"), value: d.genres.join(", ") })
                    if (d.audioLanguages.length > 0) out.push({ label: qsTr("Audio"), value: d.audioLanguages.join(", ") })
                    if (d.subtitleLanguages.length > 0) out.push({ label: qsTr("Subtitles"), value: d.subtitleLanguages.join(", ") })
                    if (d.quality.length > 0) out.push({ label: qsTr("Quality"), value: d.quality })
                    return out
                }
                visible: rows.length > 0

                SectionHeader { text: qsTr("Details") }

                Grid {
                    columns: 2
                    columnSpacing: Theme.spacingHuge
                    rowSpacing: Theme.spacingSmall
                    Repeater {
                        model: detailsTable.rows.length * 2
                        Text {
                            required property int index
                            readonly property var row: detailsTable.rows[Math.floor(index / 2)]
                            readonly property bool isLabel: index % 2 === 0
                            width: isLabel ? (Theme.tvMode ? 200 : 120) : Math.min(main.width - 170, 640)
                            text: isLabel ? row.label : row.value
                            color: isLabel ? Theme.textDisabled : Theme.text
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: isLabel ? Font.Normal : Font.DemiBold
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }

            CardRow {
                objectName: "collectionsRow"
                width: parent.width
                height: implicitHeight
                visible: count > 0
                title: qsTr("In collections")
                model: page.details ? page.details.collections : null
                delegate: ItemCard {}
            }

            CardRow {
                objectName: "similarRow"
                width: parent.width
                height: implicitHeight
                visible: count > 0
                title: qsTr("More like this")
                model: page.details ? page.details.similar : null
                delegate: ItemCard {}
            }
        }
    }

    // A text button ("More" under a long description).
    component LinkButton: ActionButton {
        id: link
        leftPadding: 0
        rightPadding: 0
        implicitHeight: Theme.fontSizeBody + Theme.spacingSmall
        background: Item {
            implicitWidth: 10
            FocusFrame { shown: link.visualFocus }
        }
    }

    // A season's tab: its name and number of episodes, filled when it is
    // the one shown.
    component SeasonTab: T.ItemDelegate {
        id: tab
        required property int index
        required property string itemId
        required property string name
        required property var indexNumber
        readonly property string label: page.seasonLabel(indexNumber, name)
        required property var childCount
        required property var unplayedCount
        required property bool favorite
        property bool selected: false
        height: ListView.view ? ListView.view.height : Theme.controlHeight
        implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset, implicitContentWidth + leftPadding + rightPadding)
        leftPadding: 14
        rightPadding: 14
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.name: label
        Accessible.role: Accessible.PageTab
        Keys.onReturnPressed: tab.clicked()
        Keys.onEnterPressed: tab.clicked()
        contentItem: Row {
            spacing: Theme.spacingSmall
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: tab.label
                color: tab.selected || tab.hovered ? Theme.text : Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                font.weight: Font.Bold
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: (tab.childCount || 0) > 0
                text: String(tab.childCount || 0)
                color: Theme.textDisabled
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Font.Bold
            }
        }
        background: Rectangle {
            radius: Theme.radius + 3
            color: tab.selected ? Qt.alpha(Theme.text, 0.12) : tab.hovered ? Qt.alpha(Theme.text, 0.05) : "transparent"
            FocusFrame { shown: tab.visualFocus; inset: 0 }
        }
        // The server's own name, when it says more than the number.
        ToolTipLabel {
            text: tab.name !== tab.label ? tab.name : ""
            shown: tab.hovered && text.length > 0
        }
    }

    // A site with the film or series: a small badge with the site's icon
    // and name, opened in the browser. Quiet until pointed at or focused.
    component ExternalLinkButton: T.AbstractButton {
        id: linkButton
        required property var modelData
        // The site's mark, best first: the icon bundled in icons/sites,
        // then the favicon the site itself serves, then its colour and
        // letter drawn here. Each one that fails to load gives way to the
        // next, so a badge never shows an empty or broken picture.
        readonly property var brand: ({
            shikimori: { icon: "icons/sites/shikimori.svg", color: "#e9ebef", ink: "#343434", mark: "S" },
            myanimelist: { icon: "icons/sites/myanimelist.svg", color: "#2e51a2", ink: "#ffffff", mark: "M" },
            anilist: { icon: "icons/sites/anilist.svg", color: "#02a9ff", ink: "#ffffff", mark: "A" },
            anidb: { color: "#2a4a7f", ink: "#ffffff", mark: "a" },
            tvdb: { icon: "icons/sites/tvdb.svg", color: "#6cd491", ink: "#10301c", mark: "tv" },
            tmdb: { icon: "icons/sites/tmdb.svg", color: "#0d253f", ink: "#01b4e4", mark: "tm" },
            imdb: { icon: "icons/sites/imdb.svg", color: "#f5c518", ink: "#000000", mark: "i" }
        })[modelData.site] || { color: Theme.textSecondary, ink: Theme.background, mark: "" }
        readonly property url faviconUrl: {
            const origin = /^https?:\/\/[^\/?#]+/i.exec(String(modelData.url))
            return origin ? origin[0] + "/favicon.ico" : ""
        }
        // Which mark the badge is on: 0 the bundled icon, 1 the favicon,
        // 2 the letter.
        property int markStage: brand.icon ? 0 : faviconUrl.toString().length > 0 ? 1 : 2
        readonly property bool lit: hovered || visualFocus || down
        objectName: "externalLink_" + modelData.site
        leftPadding: Theme.tvMode ? 6 : 4
        rightPadding: Theme.tvMode ? 12 : 9
        implicitWidth: leftPadding + linkRow.implicitWidth + rightPadding
        implicitHeight: Theme.tvMode ? 36 : 26
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.role: Accessible.Link
        Accessible.name: modelData.label
        Keys.onReturnPressed: linkButton.clicked()
        Keys.onEnterPressed: linkButton.clicked()
        onClicked: Qt.openUrlExternally(modelData.url)
        contentItem: Row {
            id: linkRow
            spacing: Theme.tvMode ? 8 : 6
            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: height
                height: Theme.tvMode ? 24 : 18
                opacity: linkButton.lit ? 1 : 0.85
                Image {
                    id: siteIcon
                    objectName: "siteIcon"
                    visible: linkButton.markStage === 0 && status === Image.Ready
                    anchors.fill: parent
                    source: linkButton.markStage === 0 ? Qt.resolvedUrl(linkButton.brand.icon) : ""
                    sourceSize.width: width * 2
                    sourceSize.height: height * 2
                    smooth: true
                    onStatusChanged: if (status === Image.Error) linkButton.markStage = linkButton.faviconUrl.toString().length > 0 ? 1 : 2
                }
                // A favicon is often 16 or 32 pixels: loaded at its own
                // size and scaled by the item.
                Image {
                    id: siteFavicon
                    objectName: "siteFavicon"
                    visible: linkButton.markStage === 1 && status === Image.Ready
                    anchors.fill: parent
                    source: linkButton.markStage === 1 ? linkButton.faviconUrl : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    smooth: true
                    mipmap: true
                    onStatusChanged: {
                        if (status === Image.Error || (status === Image.Ready && sourceSize.width <= 0))
                            linkButton.markStage = 2
                    }
                }
                Rectangle {
                    objectName: "siteLetter"
                    // Under a favicon still on its way, too.
                    visible: !siteIcon.visible && !siteFavicon.visible
                    anchors.fill: parent
                    radius: Theme.tvMode ? 6 : 5
                    color: linkButton.brand.color
                    Text {
                        anchors.centerIn: parent
                        text: linkButton.brand.mark
                        color: linkButton.brand.ink
                        font.pixelSize: Theme.tvMode ? 13 : 10
                        font.weight: Font.Black
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: linkButton.modelData.label
                color: linkButton.lit ? Theme.text : Theme.textSecondary
                font.pixelSize: Theme.tvMode ? Theme.fontSizeSmall + 2 : Theme.fontSizeSmall
                font.weight: Font.DemiBold
            }
        }
        background: Rectangle {
            radius: Theme.tvMode ? 9 : 7
            color: Qt.alpha(Theme.text, linkButton.down ? 0.16 : linkButton.lit ? 0.11 : 0.05)
            border.width: 1
            border.color: Qt.alpha(Theme.text, linkButton.lit ? 0.18 : 0.08)
            Behavior on color { ColorAnimation { duration: 120 } }
            FocusFrame { shown: linkButton.visualFocus; inset: 0 }
        }
        ToolTipLabel {
            text: qsTr("Open on %1").arg(linkButton.modelData.label)
            shown: linkButton.hovered && !linkButton.down
        }
    }

    // A person of the cast: a round photo (or the initial), the name and
    // the role.
    component PersonChip: T.AbstractButton {
        id: person
        required property var modelData
        width: Theme.tvMode ? 170 : 124
        height: ListView.view ? ListView.view.height : 150
        focusPolicy: Qt.StrongFocus
        Accessible.name: modelData.name
        Keys.onReturnPressed: person.clicked()
        Keys.onEnterPressed: person.clicked()
        contentItem: Column {
            spacing: Theme.spacingSmall
            Rectangle {
                id: photo
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.tvMode ? 130 : 96
                height: width
                radius: width / 2
                color: Theme.surfaceRaised
                // Cut round by a mask (a plain mask, no blur).
                Image {
                    id: photoImage
                    anchors.fill: parent
                    source: String(person.modelData.imageUrl || "").length > 0 ? Library.image(person.modelData.imageUrl) : ""
                    sourceSize.width: photo.width * 2
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    layer.enabled: status === Image.Ready
                    layer.effect: MultiEffect {
                        maskEnabled: true
                        maskSource: roundMask
                        maskThresholdMin: 0.5
                        maskSpreadAtMin: 1.0
                    }
                }
                Item {
                    id: roundMask
                    anchors.fill: photoImage
                    layer.enabled: true
                    visible: false
                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: "black"
                    }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: person.hovered ? Theme.accent : Theme.glassBorder
                }
                Text {
                    anchors.centerIn: parent
                    visible: photoImage.status !== Image.Ready
                    text: person.modelData.name.length > 0 ? person.modelData.name.charAt(0).toUpperCase() : ""
                    color: Theme.textDisabled
                    font.pixelSize: Theme.fontSizeTitle
                }
            }
            Text {
                width: person.width
                horizontalAlignment: Text.AlignHCenter
                text: person.modelData.name
                color: Theme.text
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Theme.fontWeightBold
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Text {
                width: person.width
                horizontalAlignment: Text.AlignHCenter
                visible: text.length > 0
                text: page.roleName(person.modelData.role || "")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
            }
        }
        background: FocusFrame { shown: person.visualFocus }
    }
}
