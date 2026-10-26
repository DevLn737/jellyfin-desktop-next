pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// A movie, series, season or episode: backdrop, poster, details, Play /
// Resume / Watch together, favorite and watched toggles, then seasons and
// episodes for a series, episodes for a season, and similar items.
RoutePage {
    id: page

    property ItemDetails details: null
    readonly property bool hasEpisodes: details && (details.type === "Series" || details.type === "Season")

    title: details && details.loaded ? details.name : ""

    Component.onCompleted: details = Library.createDetails(routeArgument, page)

    function clock(ticks) {
        const total = Math.floor(ticks / 10000000)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const pad = (n) => n < 10 ? "0" + n : "" + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    LoadingIndicator {
        anchors.centerIn: parent
        visible: page.details && page.details.loading && !page.details.loaded
    }

    Text {
        objectName: "itemError"
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.pageMargin, 600)
        visible: page.details && !page.details.loaded && text.length > 0
        text: page.details ? page.details.errorText : ""
        color: Theme.error
        font.pixelSize: Theme.fontSizeBody
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
    }

    Flickable {
        id: flick
        anchors.fill: parent
        visible: page.details && page.details.loaded
        contentHeight: content.implicitHeight + 2 * Theme.spacingHuge
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        // The backdrop sits at the top of the page and scrolls away with the
        // content; it fades into the page background at its lower edge.
        Item {
            id: backdrop
            z: -1
            width: flick.width
            height: Math.min(flick.height * 0.6, width * 9 / 16)
            Image {
                anchors.fill: parent
                source: page.details ? Library.image(page.details.backdropUrl) : ""
                sourceSize.width: width
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                opacity: status === Image.Ready ? 0.45 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }
            }
            Rectangle {
                anchors.fill: parent
                readonly property color clear: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0)
                gradient: Gradient {
                    GradientStop { position: 0.0; color: parent.clear }
                    GradientStop { position: 0.5; color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.35) }
                    GradientStop { position: 0.8; color: Qt.rgba(Theme.background.r, Theme.background.g, Theme.background.b, 0.8) }
                    GradientStop { position: 1.0; color: Theme.background }
                }
            }
        }

        ColumnLayout {
            id: content
            x: Theme.pageMargin
            y: Theme.spacingHuge
            width: flick.width - 2 * Theme.pageMargin
            spacing: Theme.spacingHuge

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingHuge

                Rectangle {
                    id: poster
                    Layout.alignment: Qt.AlignTop
                    visible: flick.width > 760
                    Layout.preferredWidth: 240
                    Layout.preferredHeight: page.details && page.details.type === "Episode"
                        ? Math.round(240 / Theme.thumbAspect) : Math.round(240 / Theme.posterAspect)
                    radius: Theme.radius
                    color: Theme.surfaceRaised
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: page.details ? Library.image(page.details.posterUrl) : ""
                        sourceSize.width: 480
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.spacing

                    Text {
                        objectName: "itemSeries"
                        Layout.fillWidth: true
                        visible: page.details && page.details.seriesName.length > 0 && page.details.type !== "Series"
                        text: page.details ? page.details.seriesName : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeLarge
                        elide: Text.ElideRight
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: Navigator.navigate("item/" + page.details.seriesId)
                        }
                    }
                    Text {
                        objectName: "itemName"
                        Layout.fillWidth: true
                        text: page.details ? page.details.name : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeHeadline
                        font.weight: Theme.fontWeightBold
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        text: {
                            if (!page.details)
                                return ""
                            const parts = []
                            if (page.details.subtitle.length > 0) parts.push(page.details.subtitle)
                            if (page.details.year) parts.push(page.details.year)
                            const runTime = Library.formatRunTime(page.details.runTimeTicks)
                            if (runTime.length > 0 && page.details.type !== "Series") parts.push(runTime)
                            if (page.details.officialRating.length > 0) parts.push(page.details.officialRating)
                            if (page.details.communityRating) parts.push("★ " + Number(page.details.communityRating).toFixed(1))
                            return parts.join("  ·  ")
                        }
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        wrapMode: Text.Wrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text.length > 0
                        text: page.details ? page.details.genres.join(", ") : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        wrapMode: Text.Wrap
                    }

                    Flow {
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSmall
                        spacing: Theme.spacing

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
                        ActionButton {
                            objectName: "watchTogetherButton"
                            visible: page.details && page.details.playTargetId.length > 0
                            iconSource: Icons.group
                            text: qsTr("Watch together")
                            onClicked: page.details.watchTogether()
                        }
                        ActionButton {
                            objectName: "playedButton"
                            iconSource: Icons.check
                            text: page.details && page.details.played ? qsTr("Watched") : qsTr("Mark watched")
                            onClicked: page.details.togglePlayed()
                        }
                        ActionButton {
                            objectName: "favoriteButton"
                            text: page.details && page.details.favorite ? qsTr("♥ Favorite") : qsTr("♡ Add to favorites")
                            onClicked: page.details.toggleFavorite()
                        }
                    }

                    Text {
                        objectName: "playTarget"
                        Layout.fillWidth: true
                        visible: page.details && page.details.type === "Series" && text.length > 0
                        text: page.details && page.details.playTargetName.length > 0
                            ? qsTr("Next: %1").arg(page.details.playTargetName) : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }

                    Text {
                        objectName: "itemOverview"
                        Layout.fillWidth: true
                        Layout.topMargin: Theme.spacingSmall
                        visible: text.length > 0
                        text: page.details ? page.details.overview : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontSizeBody
                        lineHeight: 1.25
                        wrapMode: Text.Wrap
                    }
                }
            }

            // Seasons of a series as tabs.
            ListView {
                id: seasons
                objectName: "seasonTabs"
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.controlHeight
                visible: page.details && page.details.type === "Series" && count > 0
                orientation: ListView.Horizontal
                spacing: Theme.spacingSmall
                clip: true
                keyNavigationEnabled: true
                model: page.details ? page.details.seasons : null
                delegate: ActionButton {
                    required property var model
                    required property int index
                    text: model.name
                    primary: page.details && page.details.seasonId === model.itemId
                    onClicked: page.details.seasonId = model.itemId
                }
            }

            Column {
                objectName: "episodeList"
                Layout.fillWidth: true
                visible: page.hasEpisodes && episodes.count > 0
                spacing: Theme.spacingTiny

                Repeater {
                    id: episodes
                    objectName: "episodes"
                    model: page.details ? page.details.episodes : null
                    EpisodeRow {
                        width: parent.width
                        onPlayClicked: page.details.playEpisode(index)
                    }
                }
            }

            CardRow {
                objectName: "collectionsRow"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: count > 0
                title: qsTr("In collections")
                model: page.details ? page.details.collections : null
                delegate: ItemCard {}
            }

            CardRow {
                objectName: "similarRow"
                Layout.fillWidth: true
                Layout.preferredHeight: implicitHeight
                visible: count > 0
                title: qsTr("More like this")
                model: page.details ? page.details.similar : null
                delegate: ItemCard {}
            }
        }
    }
}
