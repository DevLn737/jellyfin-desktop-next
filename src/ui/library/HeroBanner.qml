pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The banner at the top of the home page: one featured film or series at a
// time (HomeModel.featured: what the user was watching, then the newest of
// each library). A line saying why it is here, the logo or the title, year,
// genres, length and age rating, two lines of the description, then Play or
// Resume, More and a favorite toggle, with the part left to watch. The next
// item comes every few seconds, unless the pointer or the focus is on the
// banner or effects are simplified; the dots below choose one.
//
// The backdrop is not drawn here: the page draws it behind its rows
// (HeroBackdrop), wider than the margins, from `current`.
FocusScope {
    id: banner

    property var model: null
    property int current: 0
    readonly property int count: slides.count
    // Seconds per item when it changes by itself.
    property int interval: 8000

    implicitHeight: Theme.compact ? 340 : Theme.tvMode ? 520 : 440
    activeFocusOnTab: false
    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Featured")

    onCountChanged: if (current >= count) current = 0

    function show(index) {
        if (count === 0)
            return
        const hadFocus = banner.activeFocus
        current = (index + count) % count
        if (hadFocus) {
            const slide = slides.itemAt(current)
            if (slide)
                slide.focusPrimary()
        }
    }

    HoverHandler { id: hover }

    Timer {
        objectName: "heroTimer"
        interval: banner.interval
        repeat: true
        running: banner.count > 1 && banner.visible && !hover.hovered && !banner.activeFocus
                 && !Theme.reducedEffects && Qt.application.state === Qt.ApplicationActive
        onTriggered: banner.current = (banner.current + 1) % banner.count
    }

    Repeater {
        id: slides
        model: banner.model

        delegate: FocusScope {
            id: slide

            required property int index
            required property string itemId
            required property string name
            required property string type
            required property string subtitle
            required property string seriesId
            required property string seriesName
            required property string overview
            required property var year
            required property var genres
            required property string officialRating
            required property var runTimeTicks
            required property var playbackPositionTicks
            required property real progress
            required property bool favorite
            required property bool played
            required property var logoUrl

            readonly property bool shown: index === banner.current
            readonly property bool isEpisode: type === "Episode"
            readonly property bool resumable: (playbackPositionTicks || 0) > 0
            readonly property string title: isEpisode && seriesName.length > 0 ? seriesName : name

            function focusPrimary() { playButton.forceActiveFocus() }

            objectName: "heroSlide_" + index
            anchors.fill: parent
            focus: shown
            enabled: shown
            opacity: shown ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationSlow; easing.type: Theme.easing } }

            function clock(ticks) {
                const total = Math.floor(ticks / 10000000)
                const h = Math.floor(total / 3600)
                const m = Math.floor((total % 3600) / 60)
                const s = total % 60
                const pad = (n) => n < 10 ? "0" + n : "" + n
                return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
            }

            Column {
                id: info
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacingHuge
                width: Math.min(parent.width - Theme.spacingHuge * 3, Theme.tvMode ? 900 : 640)
                spacing: Theme.spacing

                // Why it is here.
                Text {
                    objectName: "heroEyebrow"
                    width: parent.width
                    text: {
                        let reason = slide.resumable ? qsTr("Continue")
                                   : slide.isEpisode ? qsTr("Next up") : qsTr("Recently added")
                        if (slide.isEpisode && slide.subtitle.length > 0)
                            reason += " · " + slide.subtitle + " · " + slide.name
                        return reason.toUpperCase()
                    }
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Theme.fontWeightBold
                    font.letterSpacing: 1.2
                    elide: Text.ElideRight
                }

                TitleLogo {
                    objectName: "heroTitle"
                    width: parent.width
                    text: slide.title
                    source: String(slide.logoUrl || "").length > 0 ? Library.image(slide.logoUrl) : ""
                    maxWidth: Math.min(parent.width, Theme.tvMode ? 560 : 420)
                    maxHeight: Theme.compact ? 80 : Theme.tvMode ? 150 : 120
                    fontSize: Theme.compact ? Theme.fontSizeHeadline : Math.round(Theme.fontSizeHeadline * 1.35)
                }

                // Year / genres / length / age rating.
                Row {
                    objectName: "heroMeta"
                    spacing: Theme.spacingSmall
                    readonly property var parts: {
                        const out = []
                        if (slide.year) out.push(String(slide.year))
                        if (slide.genres && slide.genres.length > 0) out.push(slide.genres.slice(0, 2).join(", "))
                        const length = slide.type !== "Series" ? Library.formatRunTime(slide.runTimeTicks || 0) : ""
                        if (length.length > 0) out.push(length)
                        return out
                    }
                    Repeater {
                        model: parent.parts
                        Row {
                            id: part
                            required property string modelData
                            required property int index
                            spacing: Theme.spacingSmall
                            Text {
                                visible: part.index > 0
                                text: "/"
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontSizeBody
                            }
                            Text {
                                text: part.modelData
                                color: Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                            }
                        }
                    }
                    Rectangle {
                        objectName: "heroRating"
                        visible: slide.officialRating.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: rating.implicitWidth + 2 * Theme.spacingSmall
                        height: rating.implicitHeight + Theme.spacingTiny
                        radius: Theme.radiusSmall
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textDisabled
                        Text {
                            id: rating
                            anchors.centerIn: parent
                            text: slide.officialRating
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }

                Text {
                    objectName: "heroOverview"
                    width: parent.width
                    visible: text.length > 0 && !Theme.compact
                    text: slide.overview
                    color: Theme.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    lineHeight: 1.3
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Item { width: 1; height: Theme.spacingSmall }

                Row {
                    spacing: Theme.spacing

                    ActionButton {
                        id: playButton
                        objectName: "heroPlay"
                        primary: true
                        focus: true
                        iconSource: Icons.play
                        text: slide.resumable
                            ? qsTr("Resume from %1").arg(slide.clock(slide.playbackPositionTicks))
                            : qsTr("Play")
                        onClicked: {
                            // A series plays from its own page's choice (next up).
                            if (slide.type === "Series")
                                Navigator.navigate(Library.routeFor(slide.itemId, slide.type))
                            else
                                Library.play(slide.itemId, slide.resumable ? slide.playbackPositionTicks : 0)
                        }
                    }
                    ActionButton {
                        objectName: "heroMore"
                        iconSource: Icons.info
                        text: qsTr("More")
                        onClicked: Navigator.navigate(slide.isEpisode && slide.seriesId.length > 0
                            ? Library.routeFor(slide.seriesId, "Series")
                            : Library.routeFor(slide.itemId, slide.type))
                    }
                    IconButton {
                        objectName: "heroFavorite"
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: slide.favorite ? Icons.favorite : Icons.favoriteBorder
                        toolTipText: slide.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                        onClicked: Library.setFavorite(slide.itemId, !slide.favorite)
                    }

                    // The part left to watch.
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        visible: slide.progress > 0 && !Theme.compact
                        spacing: Theme.spacingSmall
                        leftPadding: Theme.spacingSmall
                        Rectangle {
                            anchors.verticalCenter: parent.verticalCenter
                            width: 96
                            height: 3
                            radius: 1.5
                            color: Qt.alpha(Theme.text, 0.2)
                            Rectangle {
                                width: parent.width * slide.progress
                                height: parent.height
                                radius: parent.radius
                                color: Theme.progress
                            }
                        }
                        Text {
                            objectName: "heroLeft"
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                const left = Math.max(0, (slide.runTimeTicks || 0) - (slide.playbackPositionTicks || 0))
                                const minutes = Math.max(1, Math.round(left / 600000000))
                                return qsTr("%n min left", "", minutes)
                            }
                            color: Theme.textSecondary
                            font.pixelSize: Theme.fontSizeSmall
                        }
                    }
                }
            }
        }
    }

    // One dot per item; the current one is longer.
    Row {
        objectName: "heroDots"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingHuge + Theme.spacing
        spacing: Theme.spacingTiny
        visible: banner.count > 1
        Repeater {
            model: banner.count
            T.AbstractButton {
                id: dot
                required property int index
                width: Theme.tvMode ? 40 : 24
                height: Theme.tvMode ? 32 : 20
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                Accessible.name: qsTr("Item %1 of %2").arg(index + 1).arg(banner.count)
                onClicked: banner.current = index
                Keys.onReturnPressed: banner.current = index
                Keys.onEnterPressed: banner.current = index
                Rectangle {
                    anchors.centerIn: parent
                    width: dot.index === banner.current ? parent.width - 4 : 8
                    height: Theme.tvMode ? 6 : 3
                    radius: height / 2
                    color: dot.index === banner.current ? Theme.text : Qt.alpha(Theme.text, dot.hovered ? 0.6 : 0.3)
                    Behavior on width { NumberAnimation { duration: Theme.durationNormal } }
                }
                FocusFrame { shown: dot.visualFocus; radius: Theme.radiusSmall }
            }
        }
    }
}
