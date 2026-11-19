pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Effects
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// A movie, series, season or episode, in the "Depth" look: a fixed backdrop
// fading into the page; the poster (with how much of a series is watched
// under it); a line with the series or the genre and the kind; the logo or
// the title; year, seasons, episodes, length, age rating and score; the
// description; Play or Resume, Watch together, favorite and watched
// toggles, Download; the episode to watch next with the time left. Then the
// cast, the details (studio, genres, audio, subtitles, quality), the
// collections and similar items.
//
// A series or season lists its seasons and episodes in a panel on the right
// when the page is wide enough, under the header otherwise.
RoutePage {
    id: page

    property ItemDetails details: null
    readonly property bool loaded: details !== null && details.loaded
    readonly property bool hasEpisodes: details && (details.type === "Series" || details.type === "Season")
    readonly property bool isSeries: details && details.type === "Series"
    readonly property bool isEpisode: details && details.type === "Episode"
    // The episodes go into a side panel from this page width up.
    readonly property bool wide: flick.width >= (Theme.tvMode ? 1500 : 1180)
    readonly property bool sidePanel: hasEpisodes && wide

    title: loaded ? details.name : ""
    hasTopBar: true

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

    // The backdrop is fixed to the top of the page (outside the Flickable, so
    // the content scrolls over it). It fades into the page colour over a long
    // distance, and a scrim over it grows with the scrolling, so text stays
    // readable lower down. Drawn first, under the Flickable.
    Item {
        id: backdrop
        objectName: "itemBackdrop"
        z: -1
        width: page.width
        height: Math.min(page.height * 0.8, width * 9 / 16)
        visible: page.loaded
        readonly property color pageColor: Theme.background
        // 0 at the top of the page, 1 once the content has scrolled a
        // little more than the backdrop's half height.
        readonly property real scrolled: Math.max(0, Math.min(1, flick.contentY / (height * 0.6)))
        Image {
            anchors.right: parent.right
            width: parent.width * (Theme.compact ? 1 : 0.85)
            height: parent.height
            source: page.details ? Library.image(page.details.backdropUrl) : ""
            sourceSize.width: width
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignTop
            asynchronous: true
            opacity: status === Image.Ready ? 0.55 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
        }
        // Into the page colour on the left, under the text...
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: backdrop.pageColor }
                GradientStop { position: 0.2; color: Qt.alpha(backdrop.pageColor, 0.85) }
                GradientStop { position: 0.6; color: Qt.alpha(backdrop.pageColor, 0.35) }
                GradientStop { position: 1.0; color: Qt.alpha(backdrop.pageColor, 0.1) }
            }
        }
        // ... and at the bottom.
        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.0; color: Qt.alpha(backdrop.pageColor, 0.0) }
                GradientStop { position: 0.45; color: Qt.alpha(backdrop.pageColor, 0.2) }
                GradientStop { position: 0.8; color: Qt.alpha(backdrop.pageColor, 0.85) }
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
        anchors.fill: parent
        visible: page.loaded
        contentHeight: Math.max(main.implicitHeight, panel.visible ? panel.height : 0) + 2 * Theme.spacingHuge
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // Back, where the page is, search and more.
        PageTopBar {
            id: topBar
            objectName: "itemTopBar"
            x: Theme.pageMargin
            y: Theme.spacingLarge
            width: flick.width - 2 * Theme.pageMargin
            readonly property bool inSeries: page.details !== null && page.details.seriesId.length > 0
                                             && (page.details.type === "Season" || page.details.type === "Episode")
            crumbs: {
                if (!page.loaded)
                    return []
                const out = []
                if (inSeries)
                    out.push({ text: page.details.seriesName, route: Library.routeFor(page.details.seriesId, "Series") })
                out.push({ text: page.details.name })
                return out
            }
            moreEntries: inSeries ? [qsTr("Refresh"), qsTr("Go to series")] : [qsTr("Refresh")]
            onMoreActivated: (index) => {
                if (index === 0)
                    page.details.reload()
                else
                    Navigator.navigate(Library.routeFor(page.details.seriesId, "Series"))
            }
        }

        // --- The main column ------------------------------------------------
        Column {
            id: main
            x: Theme.pageMargin
            y: topBar.y + topBar.height + Theme.spacingLarge
            width: flick.width - 2 * Theme.pageMargin - (page.sidePanel ? panel.width + Theme.spacingHuge : 0)
            spacing: Theme.spacingHuge

            // Poster and the information beside it.
            Item {
                id: header
                width: parent.width
                height: Math.max(posterColumn.visible ? posterColumn.height : 0, info.implicitHeight)

                Column {
                    id: posterColumn
                    visible: main.width > 700
                    width: Theme.tvMode ? 280 : main.width > 1000 ? 220 : 180
                    spacing: Theme.spacingSmall

                    Rectangle {
                        id: poster
                        width: parent.width
                        height: page.isEpisode ? Math.round(width / Theme.thumbAspect) : Math.round(width / Theme.posterAspect)
                        radius: Theme.radiusLarge
                        color: Theme.surfaceRaised
                        border.width: 1
                        border.color: Theme.glassBorder
                        clip: true
                        Image {
                            id: posterImage
                            anchors.fill: parent
                            anchors.margins: 1
                            source: page.details ? Library.image(page.details.posterUrl) : ""
                            sourceSize.width: 480
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                        }
                        // Without a poster: the title's initial, as on the cards.
                        Text {
                            objectName: "posterInitial"
                            anchors.centerIn: parent
                            visible: posterImage.status !== Image.Ready
                            text: page.title.length > 0 ? page.title.charAt(0).toUpperCase() : ""
                            color: Theme.textDisabled
                            font.pixelSize: Theme.fontSizeHeadline * 2
                        }
                    }

                    // How much of a series or season is watched.
                    Item {
                        objectName: "watchedSummary"
                        width: parent.width
                        height: watchedText.implicitHeight + Theme.spacingSmall + 3
                        readonly property int total: page.details ? page.details.episodeCount : 0
                        readonly property int watched: page.details ? Math.max(0, total - page.details.unplayedCount) : 0
                        visible: page.hasEpisodes && total > 0
                        Text {
                            id: watchedText
                            text: qsTr("Watched %1 of %2").arg(parent.watched).arg(parent.total)
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Text {
                            anchors.right: parent.right
                            text: parent.total > 0 ? Math.round(100 * parent.watched / parent.total) + "%" : ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            width: parent.width
                            height: 3
                            radius: 1.5
                            color: Qt.alpha(Theme.text, 0.15)
                            Rectangle {
                                width: parent.parent.total > 0 ? parent.width * parent.parent.watched / parent.parent.total : 0
                                height: parent.height
                                radius: parent.radius
                                color: Theme.progress
                            }
                        }
                    }
                }

                Column {
                    id: info
                    anchors.left: posterColumn.visible ? posterColumn.right : parent.left
                    anchors.leftMargin: posterColumn.visible ? Theme.spacingHuge : 0
                    anchors.right: parent.right
                    spacing: Theme.spacing

                    // The series of a season or episode (it leads there), or
                    // the genre and the kind.
                    Text {
                        objectName: "itemSeries"
                        width: parent.width
                        visible: page.details && page.details.seriesName.length > 0 && page.details.type !== "Series"
                        text: page.details ? page.details.seriesName : ""
                        color: Theme.accent
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: Theme.fontWeightBold
                        elide: Text.ElideRight
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Navigator.navigate("item/" + page.details.seriesId)
                        }
                    }
                    Text {
                        objectName: "itemKind"
                        width: parent.width
                        visible: !(page.details && page.details.seriesName.length > 0 && page.details.type !== "Series")
                                 && text.length > 0
                        text: {
                            if (!page.details)
                                return ""
                            const parts = []
                            if (page.details.genres.length > 0) parts.push(page.details.genres[0])
                            const kind = page.kindName(page.details.type)
                            if (kind.length > 0) parts.push(kind)
                            return parts.join(" · ").toUpperCase()
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        font.weight: Theme.fontWeightBold
                        font.letterSpacing: 1.2
                        elide: Text.ElideRight
                    }

                    TitleLogo {
                        objectName: "itemName"
                        width: parent.width
                        text: page.details ? page.details.name : ""
                        // A season or episode has its own name, not the series' logo.
                        source: page.details && !page.isEpisode && page.details.type !== "Season"
                                && String(page.details.logoUrl).length > 0 ? Library.image(page.details.logoUrl) : ""
                        maxWidth: Math.min(parent.width, Theme.tvMode ? 560 : 440)
                        maxHeight: Theme.tvMode ? 160 : 120
                        fontSize: Math.round(Theme.fontSizeHeadline * 1.25)
                        maximumLineCount: 3
                    }

                    // Year / seasons / episodes / length, age rating, score.
                    Flow {
                        objectName: "itemMeta"
                        width: parent.width
                        spacing: Theme.spacingSmall
                        readonly property var parts: {
                            const d = page.details
                            if (!d)
                                return []
                            const out = []
                            if (d.subtitle.length > 0) out.push(d.subtitle)
                            if (d.year) out.push(String(d.year))
                            if (page.isSeries && d.seasons.count > 0) out.push(qsTr("%n season(s)", "", d.seasons.count))
                            if (page.hasEpisodes && d.episodeCount > 0) out.push(qsTr("%n episode(s)", "", d.episodeCount))
                            const runTime = Library.formatRunTime(d.runTimeTicks)
                            if (runTime.length > 0 && !page.hasEpisodes) out.push(runTime)
                            return out
                        }
                        Repeater {
                            model: parent.parts
                            Row {
                                id: metaPart
                                required property string modelData
                                required property int index
                                spacing: Theme.spacingSmall
                                height: ratingChip.height
                                Text {
                                    visible: metaPart.index > 0
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: "/"
                                    color: Theme.textDisabled
                                    font.pixelSize: Theme.fontSizeBody
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: metaPart.modelData
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                }
                            }
                        }
                        Rectangle {
                            id: ratingChip
                            objectName: "itemRating"
                            visible: page.details && page.details.officialRating.length > 0
                            width: ratingText.implicitWidth + 2 * Theme.spacingSmall
                            height: ratingText.implicitHeight + Theme.spacingTiny + 2
                            radius: Theme.radiusSmall
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.textDisabled
                            Text {
                                id: ratingText
                                anchors.centerIn: parent
                                text: page.details ? page.details.officialRating : ""
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                        Text {
                            visible: page.details && !!page.details.communityRating
                            height: ratingChip.height
                            verticalAlignment: Text.AlignVCenter
                            text: page.details && page.details.communityRating
                                ? "★ " + Number(page.details.communityRating).toFixed(1) : ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }

                    Text {
                        id: overview
                        objectName: "itemOverview"
                        property bool expanded: false
                        width: Math.min(parent.width, Theme.tvMode ? 1100 : 760)
                        visible: text.length > 0
                        text: page.details ? page.details.overview : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        lineHeight: 1.3
                        wrapMode: Text.Wrap
                        maximumLineCount: expanded ? 100 : 4
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
                        spacing: Theme.spacingSmall

                        ActionButton {
                            id: playButton
                            objectName: "playButton"
                            primary: true
                            focus: true
                            iconSource: Icons.play
                            visible: page.details && page.details.playTargetId.length > 0
                            text: page.details && page.details.resumePositionTicks > 0
                                ? qsTr("Resume from %1").arg(page.clock(page.details.resumePositionTicks))
                                : qsTr("Play")
                            onClicked: page.details.resumePositionTicks > 0 ? page.details.resume() : page.details.play()
                        }
                        ActionButton {
                            objectName: "playFromStartButton"
                            visible: page.details && page.details.resumePositionTicks > 0
                            text: qsTr("From the beginning")
                            onClicked: page.details.play()
                        }
                        ToggleIconButton {
                            objectName: "favoriteButton"
                            on: page.details !== null && page.details.favorite
                            iconSource: on ? Icons.favorite : Icons.favoriteBorder
                            toolTipText: on ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                            onClicked: page.details.toggleFavorite()
                        }
                        ToggleIconButton {
                            objectName: "playedButton"
                            on: page.details !== null && page.details.played
                            iconSource: Icons.check
                            toolTipText: on ? qsTr("Watched") : qsTr("Mark watched")
                            onClicked: page.details.togglePlayed()
                        }
                        DownloadActions {
                            objectName: "downloadActions"
                            itemId: page.details ? page.details.itemId : ""
                            itemType: page.loaded ? page.details.type : ""
                            itemName: page.title
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
                            width: Math.min(implicitWidth, info.width * 0.5)
                            text: {
                                const d = page.details
                                if (!d || d.type !== "Series" || d.playTargetName.length === 0)
                                    return ""
                                const what = d.playTargetSubtitle.length > 0
                                    ? d.playTargetSubtitle + " · " + d.playTargetName : d.playTargetName
                                return targetLine.progress > 0 ? what : qsTr("Next: %1").arg(what)
                            }
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeSmall
                            elide: Text.ElideRight
                        }
                        Rectangle {
                            visible: targetLine.progress > 0
                            anchors.verticalCenter: parent.verticalCenter
                            width: 120
                            height: 3
                            radius: 1.5
                            color: Qt.alpha(Theme.text, 0.15)
                            Rectangle {
                                width: parent.width * targetLine.progress
                                height: parent.height
                                radius: parent.radius
                                color: Theme.progress
                            }
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
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }

            // Seasons and episodes under the header on a narrow page.
            Item {
                id: narrowSlot
                width: parent.width
                height: page.hasEpisodes && !page.sidePanel ? episodesSection.height : 0
                visible: height > 0
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
                    height: Theme.tvMode ? 220 : 150
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

            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
                visible: detailsTable.visible
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
                            width: isLabel ? (Theme.tvMode ? 200 : 110) : Math.min(main.width - 160, 560)
                            text: isLabel ? row.label : row.value
                            color: isLabel ? Theme.textDisabled : Theme.text
                            font.pixelSize: Theme.fontSizeBody
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

        // --- Seasons and episodes in a panel on the right ---------------------
        GlassPanel {
            id: panel
            objectName: "episodesPanel"
            visible: page.sidePanel
            x: flick.width - Theme.pageMargin - width
            y: main.y
            width: Theme.tvMode ? 520 : 400
            height: Math.max(episodesSection.height + 2 * Theme.spacingLarge, Math.min(flick.height - 2 * Theme.spacingHuge, 720))
        }

        // One list of seasons and episodes; it sits in the panel or under
        // the header.
        Column {
            id: episodesSection
            objectName: "episodeList"
            visible: page.hasEpisodes
            x: page.sidePanel ? panel.x + Theme.spacingLarge : main.x
            y: page.sidePanel ? panel.y + Theme.spacingLarge : main.y + narrowSlot.y
            width: page.sidePanel ? panel.width - 2 * Theme.spacingLarge : main.width
            spacing: Theme.spacing

            Item {
                width: parent.width
                height: panelTitle.implicitHeight
                SectionHeader {
                    id: panelTitle
                    text: qsTr("Seasons and episodes")
                }
                Text {
                    anchors.right: parent.right
                    anchors.verticalCenter: panelTitle.verticalCenter
                    readonly property int total: page.details ? page.details.episodeCount : 0
                    visible: total > 0
                    text: qsTr("%1 of %2 watched").arg(Math.max(0, total - (page.details ? page.details.unplayedCount : 0))).arg(total)
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            // Seasons as tabs.
            ListView {
                id: seasons
                objectName: "seasonTabs"
                // Up or Down into the tabs lands on the chosen season
                // (SpatialNavigation), not on the tab nearest across.
                readonly property bool enterAtCurrent: true
                width: parent.width
                height: Theme.controlHeight
                visible: page.isSeries && count > 0
                orientation: ListView.Horizontal
                spacing: Theme.spacingLarge
                clip: true
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                model: page.details ? page.details.seasons : null
                delegate: SeasonTab {
                    selected: page.details !== null && page.details.seasonId === itemId
                    onClicked: page.details.seasonId = itemId
                }
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.border
                visible: seasons.visible
            }

            Column {
                width: parent.width
                spacing: Theme.spacingTiny

                Repeater {
                    id: episodes
                    objectName: "episodes"
                    model: page.details ? page.details.episodes : null
                    EpisodeRow {
                        width: parent.width
                        compact: page.sidePanel
                        onPlayClicked: page.details.playEpisode(index)
                    }
                }
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

    // A season's tab: its name, underlined when it is the one shown.
    component SeasonTab: T.AbstractButton {
        id: tab
        required property string itemId
        required property string name
        property bool selected: false
        height: ListView.view ? ListView.view.height : Theme.controlHeight
        implicitWidth: tabText.implicitWidth
        focusPolicy: Qt.StrongFocus
        Accessible.name: name
        Accessible.role: Accessible.PageTab
        Keys.onReturnPressed: tab.clicked()
        Keys.onEnterPressed: tab.clicked()
        contentItem: Text {
            id: tabText
            text: tab.name
            verticalAlignment: Text.AlignVCenter
            color: tab.selected ? Theme.text : tab.hovered ? Theme.text : Theme.textSecondary
            font.pixelSize: Theme.fontSizeBody
            font.weight: tab.selected ? Theme.fontWeightBold : Theme.fontWeightNormal
        }
        background: Item {
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: 2
                radius: 1
                color: Theme.accent
                visible: tab.selected
            }
            FocusFrame { shown: tab.visualFocus }
        }
    }

    // A person of the cast: a round photo (or the initial), the name and
    // the role.
    component PersonChip: T.AbstractButton {
        id: person
        required property var modelData
        width: Theme.tvMode ? 150 : 100
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
                width: Theme.tvMode ? 110 : 72
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
