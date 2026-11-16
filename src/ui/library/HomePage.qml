pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Home: the user's libraries, then Continue watching, Next up and the
// latest items of each library. Up and Down move between rows, Left and
// Right along a row. Coming back to the page refreshes the rows in place.
// Without the server, when there are downloads, the page shows them
// instead: a "No connection to the server" banner and rows of what is
// downloaded (continue watching, films, episodes, music, other), which
// play from disk.
RoutePage {
    id: page

    readonly property HomeModel home: Library.home
    // The server cannot be reached (the connection dropped, or the home
    // page could not load at all) and something is downloaded.
    readonly property bool offlineHome: Downloads.available && Downloads.hasDownloads
        && (!Library.online || (home !== null && home.errorText.length > 0 && home.count === 0))

    title: qsTr("Home")
    hasTopBar: true

    readonly property Item heroBanner: rows.headerItem ? rows.headerItem.heroBanner : null

    // The banner arrives after the first rows, and the header changes height
    // with the window: a list the user has not moved stays at its top (the
    // view would otherwise keep the first row in place and push the banner
    // up out of sight).
    property bool movedByUser: false
    Connections {
        target: rows.headerItem
        function onHeightChanged() {
            if (!page.movedByUser)
                rows.positionViewAtBeginning()
        }
    }

    // Today, for the date over the banner; set again when the page shows.
    property date today: new Date()

    Component.onCompleted: if (home) home.reload()
    Basic.StackView.onActivated: {
        today = new Date()
        if (home && home.count > 0) home.reload()
    }

    // Back from a lost connection: what failed loads again.
    Connections {
        target: Library
        function onReconnected() { if (page.home && page.home.errorText.length > 0) page.home.reload() }
    }

    // The featured item's backdrop, behind the rows and wider than their
    // margins: it scrolls away with the banner and fades into the page.
    HeroBackdrop {
        objectName: "homeHeroBackdrop"
        z: -1
        width: page.width
        height: page.heroBanner ? page.heroBanner.y + page.heroBanner.height + Theme.spacingHuge * 2 : 0
        y: -(rows.contentY - rows.originY)
        visible: !page.offlineHome && page.heroBanner !== null && page.heroBanner.visible
        model: page.home ? page.home.featured : null
        current: page.heroBanner ? page.heroBanner.current : 0
    }

    ListView {
        SmoothWheel {}
        id: rows
        objectName: "homeRows"
        visible: !page.offlineHome
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        focus: !page.offlineHome
        clip: true
        spacing: Theme.spacingHuge
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: Theme.durationNormal
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: Theme.spacingHuge
        preferredHighlightEnd: height - Theme.spacingHuge
        // Rows are tall; keep one more on each side ready.
        cacheBuffer: 600
        model: page.home
        onMovementStarted: page.movedByUser = true
        onCurrentIndexChanged: if (currentIndex > 0) page.movedByUser = true

        header: Column {
            readonly property alias heroBanner: hero
            width: rows.width
            topPadding: Theme.spacingLarge
            bottomPadding: Theme.spacingHuge
            spacing: Theme.spacingLarge

            StateMessage {
                objectName: "homeError"
                width: parent.width
                height: visible ? 200 : 0
                visible: page.home && page.home.errorText.length > 0
                kind: Library.online ? "error" : "offline"
                description: page.home ? page.home.errorText : ""
                onRetry: if (page.home) page.home.reload()
            }

            // The date, the search field (it opens Ctrl+K) and the
            // home page's settings.
            Item {
                objectName: "homeTopBar"
                width: parent.width
                height: Theme.controlHeight
                Text {
                    objectName: "homeDate"
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    text: {
                        const date = Qt.locale().toString(page.today, "dddd, d MMMM")
                        return date.charAt(0).toUpperCase() + date.slice(1)
                    }
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    visible: !Theme.compact
                }
                Row {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSmall
                    T.AbstractButton {
                        id: searchField
                        objectName: "homeSearch"
                        width: Theme.compact ? 220 : Theme.tvMode ? 480 : 300
                        height: Theme.controlHeight
                        focusPolicy: Qt.StrongFocus
                        activeFocusOnTab: true
                        Accessible.role: Accessible.EditableText
                        Accessible.name: qsTr("Search the library")
                        onClicked: CommandRegistry.requestPalette()
                        Keys.onReturnPressed: CommandRegistry.requestPalette()
                        Keys.onEnterPressed: CommandRegistry.requestPalette()
                        KeyNavigation.down: hero.count > 0 ? hero : libraries
                        background: Rectangle {
                            radius: Theme.radiusLarge
                            color: searchField.hovered ? Theme.surfaceHover : Theme.panelFill
                            border.width: 1
                            border.color: Theme.glassBorder
                            FocusFrame { shown: searchField.visualFocus; radius: Theme.radiusLarge }
                        }
                        contentItem: Item {
                            Icon {
                                id: searchIcon
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.spacing
                                anchors.verticalCenter: parent.verticalCenter
                                size: Theme.iconSize - 4
                                source: Icons.search
                                color: Theme.textSecondary
                                opacity: 0.8
                            }
                            Text {
                                anchors.left: searchIcon.right
                                anchors.leftMargin: Theme.spacingSmall
                                anchors.right: shortcut.left
                                anchors.verticalCenter: parent.verticalCenter
                                text: qsTr("Search the library")
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontSizeBody
                                elide: Text.ElideRight
                            }
                            Rectangle {
                                id: shortcut
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingSmall
                                anchors.verticalCenter: parent.verticalCenter
                                visible: !Theme.tvMode
                                width: shortcutText.implicitWidth + Theme.spacingSmall
                                height: shortcutText.implicitHeight + 4
                                radius: Theme.radiusSmall
                                color: "transparent"
                                border.width: 1
                                border.color: Theme.border
                                Text {
                                    id: shortcutText
                                    anchors.centerIn: parent
                                    text: qsTr("Ctrl K")
                                    color: Theme.textDisabled
                                    font.pixelSize: Theme.fontSizeSmall - 1
                                }
                            }
                        }
                    }
                    IconButton {
                        id: customize
                        objectName: "customizeHomeButton"
                        iconSource: Icons.settings
                        toolTipText: qsTr("Customize home")
                        onClicked: Navigator.navigate("customizeHome")
                        KeyNavigation.down: hero.count > 0 ? hero : libraries
                    }
                }
            }

            HeroBanner {
                id: hero
                objectName: "homeHero"
                width: parent.width
                visible: count > 0
                height: visible ? implicitHeight : 0
                model: page.home ? page.home.featured : null
                KeyNavigation.down: libraries
            }

            CardRow {
                id: libraries
                objectName: "librariesRow"
                width: parent.width
                visible: count > 0
                title: qsTr("My media")
                model: page.home ? page.home.libraries : null
                cardHeight: Math.round(Theme.thumbWidth / Theme.thumbAspect) + Theme.cardCaptionHeight
                delegate: ItemCard { shape: "thumb" }
                KeyNavigation.down: rows
            }

            // While the first rows load: grey cards where they will be.
            Column {
                objectName: "homeSkeleton"
                width: parent.width
                spacing: Theme.spacingLarge
                visible: page.home && page.home.loading && page.home.count === 0
                Repeater {
                    model: visible ? 2 : 0
                    Column {
                        width: parent.width
                        spacing: Theme.spacingSmall
                        SkeletonBox { width: 180; height: 20 }
                        Row {
                            spacing: Theme.spacing
                            Repeater {
                                model: 6
                                SkeletonBox {
                                    width: Theme.thumbWidth
                                    height: Math.round(Theme.thumbWidth / Theme.thumbAspect)
                                    radius: Theme.radiusLarge
                                }
                            }
                        }
                    }
                }
            }
        }

        delegate: CardRow {
            id: row
            required title
            required property string kind
            required property string sectionId
            required property var items
            required property string cardShape
            required property int index

            objectName: "homeRow_" + (kind === "recommend" || kind === "section" ? sectionId : kind)
            width: rows.width
            focus: ListView.isCurrentItem
            model: items
            cardHeight: Math.round(Theme.cardWidthFor(cardShape) / Theme.cardAspectFor(cardShape)) + Theme.cardCaptionHeight
            delegate: ItemCard { shape: row.cardShape }
        }

        footer: Item { width: 1; height: Theme.spacingHuge }
    }

    StateMessage {
        objectName: "homeEmpty"
        anchors.centerIn: parent
        visible: !page.offlineHome && page.home && !page.home.loading && page.home.count === 0
                 && page.home.libraries.count === 0 && page.home.errorText.length === 0
        kind: "empty"
        description: qsTr("Nothing here yet. Add media to a library on the server.")
    }

    // Without the server: the downloads, as rows of cards.
    ListView {
        SmoothWheel {}
        id: offlineRows
        objectName: "offlineRows"
        visible: page.offlineHome
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        focus: page.offlineHome
        clip: true
        spacing: Theme.spacingHuge
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: Theme.durationNormal
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: Theme.spacingHuge
        preferredHighlightEnd: height - Theme.spacingHuge
        // Only the rows that have something.
        model: [
            { key: "resume", title: qsTr("Continue watching"), items: Downloads.resumeRow, shape: "thumb" },
            { key: "movies", title: qsTr("Downloaded films"), items: Downloads.moviesRow, shape: "poster" },
            { key: "episodes", title: qsTr("Downloaded episodes"), items: Downloads.episodesRow, shape: "thumb" },
            { key: "music", title: qsTr("Downloaded music"), items: Downloads.musicRow, shape: "square" },
            { key: "other", title: qsTr("Other downloads"), items: Downloads.otherRow, shape: "thumb" },
        ].filter(row => Downloads.revision >= 0 && row.items && row.items.count > 0)

        header: Column {
            width: offlineRows.width
            topPadding: Theme.spacingLarge
            bottomPadding: Theme.spacingLarge
            spacing: Theme.spacing

            Rectangle {
                objectName: "offlineBanner"
                width: parent.width
                height: bannerRow.implicitHeight + 2 * Theme.spacing
                radius: Theme.radius
                color: Theme.surfaceRaised
                Accessible.role: Accessible.AlertMessage
                Accessible.name: bannerTitle.text + ". " + bannerText.text

                Row {
                    id: bannerRow
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacing
                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        source: Icons.error
                        color: Theme.textSecondary
                    }
                    Column {
                        width: parent.width - 2 * Theme.iconSize - 2 * Theme.spacing - downloadsLink.width
                        anchors.verticalCenter: parent.verticalCenter
                        Text {
                            id: bannerTitle
                            width: parent.width
                            text: qsTr("No connection to the server")
                            color: Theme.text
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Theme.fontWeightBold
                            wrapMode: Text.Wrap
                        }
                        Text {
                            id: bannerText
                            width: parent.width
                            text: qsTr("Your downloads play from this computer. What you watch now reaches the server when the connection is back.")
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                            wrapMode: Text.Wrap
                        }
                    }
                    ActionButton {
                        id: downloadsLink
                        objectName: "offlineDownloadsButton"
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: Icons.download
                        text: qsTr("Downloads")
                        onClicked: Navigator.navigate("downloads")
                        KeyNavigation.down: offlineRows
                    }
                }
            }
        }

        delegate: CardRow {
            id: offlineRow
            required property var modelData
            required property int index

            objectName: "offlineRow_" + modelData.key
            width: offlineRows.width
            focus: ListView.isCurrentItem
            title: modelData.title
            model: modelData.items
            cardHeight: Math.round(Theme.cardWidthFor(modelData.shape) / Theme.cardAspectFor(modelData.shape)) + 52
            delegate: ItemCard {
                shape: offlineRow.modelData.shape
                playOnClick: true
            }
        }

        footer: Item { width: 1; height: Theme.spacingHuge }
    }
}
