pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as Basic
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

    Component.onCompleted: if (home) home.reload()
    Basic.StackView.onActivated: if (home && home.count > 0) home.reload()

    // Back from a lost connection: what failed loads again.
    Connections {
        target: Library
        function onReconnected() { if (page.home && page.home.errorText.length > 0) page.home.reload() }
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

        header: Column {
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

            Item {
                width: parent.width
                height: customize.implicitHeight
                ActionButton {
                    id: customize
                    objectName: "customizeHomeButton"
                    anchors.right: parent.right
                    iconSource: Icons.settings
                    text: qsTr("Customize home")
                    onClicked: Navigator.navigate("customizeHome")
                    KeyNavigation.down: libraries
                }
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
