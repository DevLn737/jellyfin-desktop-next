pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// The banner at the top of the home page: one featured film or series at a
// time (HomeModel.featured: the last few things the user started, then a
// couple of new ones nobody started). A line saying why it is here
// ("Continue watching", "Next episode", "New"), the logo or the title, year,
// genres, length and age rating, two lines of the description, then Play or
// Resume, More and a favorite toggle, with the part left to watch. The next
// item comes by itself every few seconds, also in TV mode and with simplified
// effects; moving the pointer over the banner starts the wait again, and it
// stays put while the focus is on the banner (the user is choosing). The
// dots below choose one, also by focusing them, so Left and Right along the
// dots go through the items.
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
    // Room kept free: the bar over the banner's top, the row that overlaps
    // its bottom, and the page margin beside it (the arrows sit in it).
    property real topInset: 0
    property real bottomInset: Theme.spacingHuge
    property real sideInset: 0

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

    // Called on any sign of the user: the next change waits a full interval.
    function postpone() {
        if (timer.running) {
            timer.restart()
            fill.restart()
        }
    }

    // Shows the arrows. Moving the pointer does not hold the next item
    // back: only a press does. The arrows sit in the page margins, so the
    // area reaches out to them.
    HoverHandler {
        id: hover
        margin: banner.sideInset
    }
    // A press anywhere on the banner, buttons included (a passive grab):
    // the next change waits a full interval.
    PointHandler {
        acceptedButtons: Qt.AllButtons
        onActiveChanged: if (active) banner.postpone()
    }

    Timer {
        id: timer
        objectName: "heroTimer"
        interval: banner.interval
        repeat: true
        // Not while the focus is on the banner: the user is choosing there.
        running: banner.count > 1 && banner.visible && !banner.activeFocus
                 && Qt.application.state === Qt.ApplicationActive
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
            required property string featuredReason
            required property var communityRating
            required property int childCount

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
                anchors.bottomMargin: banner.bottomInset
                width: Math.min(parent.width - Theme.spacingHuge * 3, Theme.tvMode ? 1000 : 760)
                spacing: Theme.spacing

                // Why it is here: "CONTINUE · S1 E3 «NAME»".
                Text {
                    objectName: "heroEyebrow"
                    width: parent.width
                    text: {
                        let reason = slide.featuredReason === "resume" ? qsTr("Continue watching")
                                   : slide.featuredReason === "nextUp" ? qsTr("Next episode")
                                   : qsTr("New")
                        if (slide.isEpisode && slide.subtitle.length > 0)
                            reason += " · " + slide.subtitle + (slide.name.length > 0 ? " «" + slide.name + "»" : "")
                        return reason.toUpperCase()
                    }
                    color: Theme.accent
                    font.pixelSize: Theme.fontSizeSmall + 1
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 1.6
                    elide: Text.ElideRight
                }

                TitleLogo {
                    objectName: "heroTitle"
                    width: parent.width
                    text: slide.title
                    source: String(slide.logoUrl || "").length > 0 ? Library.image(slide.logoUrl) : ""
                    maxWidth: Math.min(parent.width, Theme.tvMode ? 760 : 600)
                    maxHeight: Theme.compact ? 96 : Theme.tvMode ? 220 : 180
                    fontSize: Theme.compact ? Theme.fontSizeHeadline + 8 : Theme.tvMode ? 96 : 76
                    fontWeight: Font.Black
                }

                // Genres.
                Flow {
                    objectName: "heroGenres"
                    width: parent.width
                    spacing: Theme.spacingSmall
                    visible: slide.genres && slide.genres.length > 0 && !Theme.compact
                    Repeater {
                        model: slide.genres ? slide.genres.slice(0, 3) : []
                        Chip { required property string modelData; text: modelData }
                    }
                }

                // Year, seasons or length, age rating, score.
                Row {
                    objectName: "heroMeta"
                    spacing: Theme.spacingLarge
                    readonly property var parts: {
                        const out = []
                        if (slide.year) out.push(String(slide.year))
                        if (slide.type === "Series") {
                            if (slide.childCount > 0) out.push(qsTr("%n season(s)", "", slide.childCount))
                        } else {
                            const length = Library.formatRunTime(slide.runTimeTicks || 0)
                            if (length.length > 0) out.push(length)
                        }
                        if (slide.officialRating.length > 0) out.push(slide.officialRating)
                        return out
                    }
                    Repeater {
                        model: parent.parts
                        Text {
                            required property string modelData
                            text: modelData
                            color: Theme.text
                            opacity: 0.86
                            font.pixelSize: Theme.fontSizeLarge
                            font.weight: Font.DemiBold
                        }
                    }
                    Row {
                        objectName: "heroScore"
                        visible: (slide.communityRating || 0) > 0
                        spacing: 4
                        Text {
                            text: "★"
                            color: "#f5c04a"
                            font.pixelSize: Theme.fontSizeLarge
                        }
                        Text {
                            text: Number(slide.communityRating || 0).toLocaleString(Qt.locale(), "f", 1)
                            color: Theme.text
                            opacity: 0.86
                            font.pixelSize: Theme.fontSizeLarge
                            font.weight: Font.DemiBold
                        }
                    }
                }

                Text {
                    objectName: "heroOverview"
                    width: Math.min(parent.width, Theme.tvMode ? 900 : 640)
                    visible: text.length > 0 && !Theme.compact
                    text: slide.overview
                    color: Theme.text
                    opacity: 0.78
                    font.pixelSize: Theme.fontSizeBody + 1
                    lineHeight: 1.35
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                }

                Item { width: 1; height: Theme.spacingSmall }

                Row {
                    spacing: Theme.spacing
                    readonly property int buttonHeight: Theme.tvMode ? 72 : Theme.compact ? Theme.controlHeight : 48

                    ActionButton {
                        id: playButton
                        objectName: "heroPlay"
                        primary: true
                        focus: true
                        implicitHeight: parent.buttonHeight
                        leftPadding: Theme.spacingHuge - 4
                        rightPadding: Theme.spacingHuge - 4
                        font.pixelSize: Theme.fontSizeLarge - 1
                        iconSource: Icons.play
                        progress: slide.progress > 0 ? slide.progress : -1
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
                    IconButton {
                        objectName: "heroFavorite"
                        square: true
                        implicitWidth: parent.buttonHeight
                        implicitHeight: parent.buttonHeight
                        iconSource: slide.favorite ? Icons.favorite : Icons.favoriteBorder
                        toolTipText: slide.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                        onClicked: Library.setFavorite(slide.itemId, !slide.favorite)
                    }
                    IconButton {
                        objectName: "heroPlayed"
                        square: true
                        visible: slide.type !== "Series"
                        implicitWidth: parent.buttonHeight
                        implicitHeight: parent.buttonHeight
                        iconSource: Icons.check
                        opacity: slide.played ? 1 : 0.75
                        toolTipText: slide.played ? qsTr("Mark as not watched") : qsTr("Mark as watched")
                        onClicked: Library.setPlayed(slide.itemId, !slide.played, slide.favorite)
                    }
                    ActionButton {
                        objectName: "heroMore"
                        implicitHeight: parent.buttonHeight
                        font.pixelSize: Theme.fontSizeLarge - 1
                        iconSource: Icons.info
                        text: qsTr("More")
                        onClicked: Navigator.navigate(slide.isEpisode && slide.seriesId.length > 0
                            ? Library.routeFor(slide.seriesId, "Series")
                            : Library.routeFor(slide.itemId, slide.type))
                    }
                }
            }
        }
    }

    // Previous and next, for the pointer (keys go along the dots).
    IconButton {
        objectName: "heroPrevious"
        visible: banner.count > 1 && !Theme.tvMode && (hover.hovered || hovered)
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        anchors.left: parent.left
        anchors.leftMargin: -banner.sideInset + Math.max(4, (banner.sideInset - width) / 2)
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: banner.topInset / 2 - banner.bottomInset / 2
        square: true
        iconSource: Icons.left
        toolTipText: qsTr("Previous")
        onClicked: banner.show(banner.current - 1)
    }
    IconButton {
        objectName: "heroNext"
        visible: banner.count > 1 && !Theme.tvMode && (hover.hovered || hovered)
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        anchors.right: parent.right
        anchors.rightMargin: -banner.sideInset + Math.max(4, (banner.sideInset - width) / 2)
        anchors.verticalCenter: parent.verticalCenter
        anchors.verticalCenterOffset: banner.topInset / 2 - banner.bottomInset / 2
        square: true
        iconSource: Icons.right
        toolTipText: qsTr("Next")
        onClicked: banner.show(banner.current + 1)
    }

    // One dot per item; the current one is longer and fills while the item
    // waits for the next.
    Row {
        objectName: "heroDots"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: banner.bottomInset + Theme.spacing
        spacing: Theme.spacingTiny
        visible: banner.count > 1
        Repeater {
            model: banner.count
            T.AbstractButton {
                id: dot
                required property int index
                readonly property bool active: index === banner.current
                width: active ? (Theme.tvMode ? 64 : 44) : (Theme.tvMode ? 28 : 18)
                height: Theme.tvMode ? 32 : 20
                focusPolicy: Qt.StrongFocus
                activeFocusOnTab: true
                Accessible.name: qsTr("Item %1 of %2").arg(index + 1).arg(banner.count)
                onClicked: banner.current = index
                onActiveFocusChanged: if (activeFocus) banner.current = index
                Keys.onReturnPressed: banner.current = index
                Keys.onEnterPressed: banner.current = index
                Behavior on width { NumberAnimation { duration: Theme.durationNormal } }
                Rectangle {
                    anchors.centerIn: parent
                    width: dot.active ? parent.width - 4 : (Theme.tvMode ? 10 : 7)
                    height: Theme.tvMode ? 10 : 7
                    radius: height / 2
                    color: Qt.alpha(Theme.text, dot.active ? 0.3 : dot.hovered ? 0.6 : 0.35)
                    Rectangle {
                        objectName: "heroDotFill"
                        visible: dot.active
                        width: parent.width * (timer.running ? banner.elapsed : 1)
                        height: parent.height
                        radius: parent.radius
                        color: Theme.text
                    }
                }
                FocusFrame { shown: dot.visualFocus; radius: Theme.radiusSmall }
            }
        }
    }

    // How much of the wait for the next item has passed (0 to 1).
    property real elapsed: 0
    NumberAnimation {
        id: fill
        target: banner
        property: "elapsed"
        from: 0
        to: 1
        duration: banner.interval
        running: timer.running
    }
    onCurrentChanged: if (fill.running) fill.restart()
}
