pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Group
import Jfd.Ui.Player

// The player: video under an on-screen display that hides after a few
// seconds without the mouse. Top: back, title and the group indicator.
// Bottom: the timeline, play/pause, jumps, next, volume, track and playback
// menus, fullscreen. A skip button shows during intros and credits.
//
// Keys: Space or K play/pause, ←/→ ∓10 s, ↑/↓ volume, M mute, F fullscreen,
// S skip, N next, E episodes, P picture in picture, A audio, C subtitles, J/L subtitle delay ∓0.1 s,
// Shift+J/L audio delay, Esc back.
//
// TV mode: the display is a focus mode. Hidden, or with nothing on it
// focused, Left and Right seek (held: faster and faster, one seek when let
// go), Up and Down show it with the timeline focused and Select plays or
// pauses; shown, the arrows move between its controls. The menus are one row of named buttons
// (Episodes, Subtitles, Audio, Settings). Back closes a menu, then the
// display, then the player. On a Live TV channel: Page Up / Channel
// Up (and a gamepad's LB) the next channel, Page Down / Channel Down (RB) the
// one before; N the next channel.
RoutePage {
    id: page

    // Everything on the video, the controls included, keeps the dark palette
    // in every theme (see Theme.colorsFor).
    readonly property var colorScope: Overlay.colors

    readonly property var player: PlayerSession
    readonly property bool fullscreen: Window.window !== null && Window.window.visibility === Window.FullScreen
    property bool osdVisible: true
    // "audio", "subtitles", "tune", "episodes" or "".
    property string openMenu: ""
    // The page the settings menu opens on ("subtitles" from the tracks menu).
    property string tunePage: ""
    // The statistics overlay (right-click on the video, or I).
    property bool statsShown: false
    readonly property bool osdShown: osdVisible || openMenu.length > 0 || player.paused || !player.active

    title: player.title
    focus: true

    // The bar's buttons and the timeline take focus from Tab, never from a
    // click, so the player's keys (Space, arrows) keep working after a
    // click on them.
    component OsdButton: IconButton {
        focusPolicy: Qt.TabFocus
    }

    // A closed menu gives the keys back to the page, in TV mode to the
    // button that opened it.
    onOpenMenuChanged: {
        if (openMenu.length > 0) return
        if (tvMode && menuOpener && menuOpener.visible) menuOpener.forceActiveFocus(Qt.TabFocusReason)
        else page.forceActiveFocus()
        menuOpener = null
    }

    readonly property bool tvMode: Theme.tvMode
    // TV mode keeps the controls out of the screen's outer 5 %.
    readonly property int safeX: Math.round(width * Theme.safeMargin)
    readonly property int safeY: Math.round(height * Theme.safeMargin)
    property Item menuOpener: null
    // A held Left or Right in TV mode: presses so far and where the one
    // seek goes once it is let go.
    property int holdCount: 0
    property real seekTarget: 0
    property bool seekPending: false
    // The display hides: the arrows seek again.
    onOsdShownChanged: if (!osdShown && tvMode && osdFocused()) focusPage()

    // The keys to the page itself: the control inside that had the focus
    // lets it go (forceActiveFocus alone would give it back, the page being
    // a focus scope).
    function focusPage() {
        const focused = Window.activeFocusItem
        if (focused && focused !== page) focused.focus = false
        page.forceActiveFocus()
    }

    // Something on the display (a button, the timeline, a menu) has the
    // focus, not the page itself.
    function osdFocused() {
        const focused = Window.activeFocusItem
        if (!focused || focused === page) return false
        for (let item = focused.parent; item; item = item.parent)
            if (item === page) return true
        return false
    }

    // TV mode's seek: a press goes 10 s at once; held, the steps grow
    // (10, 30, then 60 s) and the timeline shows where, until let go.
    function seekStep(direction, repeat) {
        if (player.live) return
        // A new press, or a repeat after the last hold ended.
        if (!repeat || !holdTimer.running) {
            holdCount = 0
            seekPending = false
            seekTarget = Math.max(0, Math.min(player.duration, player.position + 10 * direction))
            player.seekBy(10 * direction)
            holdTimer.restart()
            return
        }
        holdCount++
        const step = holdCount < 10 ? 10 : holdCount < 25 ? 30 : 60
        seekTarget = Math.max(0, Math.min(player.duration, seekTarget + step * direction))
        seekPending = true
        notify(clock(seekTarget) + " / " + clock(player.duration))
        holdTimer.restart()
    }

    function commitSeek() {
        holdTimer.stop()
        holdCount = 0
        if (!seekPending) return
        seekPending = false
        player.seek(seekTarget)
    }

    // Up or Down: the display, with the timeline (a channel: play/pause)
    // focused.
    function focusOsd() {
        poke()
        if (timeline.visible) timeline.forceActiveFocus(Qt.TabFocusReason)
        else playPauseButton.forceActiveFocus(Qt.TabFocusReason)
    }

    // The first control of the open menu.
    function focusMenu() {
        const menu = openMenu === "episodes" ? sheetLoader.item : menuLoader.item
        if (!menu) return
        if (menu.focusFirst !== undefined) {
            menu.focusFirst()
            return
        }
        const first = menu.nextItemInFocusChain(true)
        for (let item = first; item; item = item.parent) {
            if (item === menu) {
                first.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
    }

    // TV mode's keys before the others; true when taken.
    function tvKey(event) {
        switch (event.key) {
        case Qt.Key_Left:
        case Qt.Key_Right:
            seekStep(event.key === Qt.Key_Right ? 1 : -1, event.isAutoRepeat)
            return true
        case Qt.Key_Up:
        case Qt.Key_Down:
            focusOsd()
            return true
        // Select with nothing on the display focused: play or pause.
        case Qt.Key_Return:
        case Qt.Key_Enter:
        case Qt.Key_Select:
            player.togglePause()
            poke()
            return true
        case Qt.Key_Escape:
        case Qt.Key_Back:
            if (openMenu.length > 0) {
                openMenu = ""
            } else if (osdVisible || osdFocused()) {
                osdVisible = false
                osdTimer.stop()
                focusPage()
            } else {
                leave()
            }
            return true
        }
        return false
    }

    function clock(seconds) {
        const total = Math.max(0, Math.floor(seconds))
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const pad = (n) => n < 10 ? "0" + n : "" + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : m + ":" + pad(s)
    }

    // Hours and minutes in the system's style, never seconds: the clock and
    // the end time would otherwise change every second.
    function shortTime(date) {
        const format = Qt.locale().timeFormat(Locale.ShortFormat).replace(/[:.\s]*s{1,2}/g, "")
        return Qt.formatTime(date, format)
    }

    // The time now, for the clock and for when the file ends.
    property date now: new Date()
    readonly property bool hasEnd: player.active && player.duration > 0
    // Remaining media time at the chosen speed (a group's base speed, not
    // the short corrections that keep it in step).
    readonly property date endsAt: new Date(now.getTime()
        + Math.max(0, player.duration - player.position) / Math.max(0.1, player.baseRate) * 1000)
    // In a group the chosen episode goes to the group's queue.
    readonly property bool hasEpisodes: player.episodes.length > 1
    // The episode that plays, from the series' list (null for a film).
    readonly property var currentEpisode: {
        const list = player.episodes
        for (let i = 0; i < list.length; ++i)
            if (list[i].current) return list[i]
        return null
    }
    // An episode's title is "Series – Episode": the series on the first
    // line, the episode under it.
    readonly property string mainTitle: {
        const separator = player.title.indexOf(" – ")
        return currentEpisode && separator > 0 ? player.title.slice(0, separator) : player.title
    }
    readonly property string episodeLine: {
        const episode = currentEpisode
        if (!episode) return ""
        const name = episode.name || ""
        if (episode.season > 0 && episode.number > 0)
            return qsTr("Season %1 · Episode %2 «%3»").arg(episode.season).arg(episode.number).arg(name)
        if (episode.number > 0) return qsTr("Episode %1 «%2»").arg(episode.number).arg(name)
        return name
    }
    // The subtitles that show, short: "Rus" (nothing when off).
    readonly property string subtitleLabel: {
        const list = player.subtitleTracks
        for (let i = 0; i < list.length; ++i) {
            const track = list[i]
            if (track.index !== player.subtitleIndex) continue
            const name = (track.language || track.title || "").toString()
            return name.length > 0 ? name.charAt(0).toUpperCase() + name.slice(1, 3) : qsTr("On")
        }
        return ""
    }
    // The chapter that plays and its place: "Chapter name · 2/5".
    readonly property int chapterIndex: {
        const list = player.chapters
        let found = -1
        for (let i = 0; i < list.length; ++i)
            if (list[i].start <= player.position) found = i
        return found
    }

    function playEpisode(itemId) {
        if (player.inGroup) GroupSession.watchTogether(itemId, "", 0)
        else player.playEpisode(itemId)
    }

    function poke() {
        osdVisible = true
        osdTimer.restart()
    }

    function toggleFullscreen() {
        const window = Window.window
        if (!window) return
        window.visibility = fullscreen ? Window.Windowed : Window.FullScreen
    }

    // A media segment's name, as the seek bar's line says it.
    function segmentName(type) {
        return type === "Intro" ? qsTr("Intro")
            : type === "Outro" ? qsTr("Credits")
            : type === "Recap" ? qsTr("Recap")
            : type === "Preview" ? qsTr("Preview")
            : type === "Commercial" ? qsTr("Commercial")
            : ""
    }

    function toggleMenu(name) {
        const opening = openMenu !== name
        if (opening && tvMode && osdFocused()) menuOpener = Window.activeFocusItem
        openMenu = opening ? name : ""
        poke()
        if (opening && tvMode) focusMenu()
    }

    // A short line in the middle of the screen for a key's effect.
    function notify(text) {
        noticeText.text = text
        noticeTimer.restart()
    }

    function delayText(seconds) {
        return (seconds > 0 ? "+" : "") + Number(seconds).toLocaleString(Qt.locale(), "f", 1) + " " + qsTr("s")
    }

    // Live TV: up or down the channel list; the line names the channel.
    function changeChannel(step) {
        if (!player.canChangeChannel) return
        player.changeChannel(step)
        notify(player.channelNumber.length > 0 ? player.channelNumber + "  " + player.channelName
                                               : player.channelName)
    }

    function changeVolume(step) {
        player.muted = false
        player.volume = player.volume + step
        notify(qsTr("Volume %1%").arg(player.volume))
    }

    function toggleMute() {
        player.muted = !player.muted
        notify(player.muted ? qsTr("Sound off") : qsTr("Volume %1%").arg(player.volume))
    }

    function shiftSubtitles(step) {
        player.subtitleDelay = Math.round((player.subtitleDelay + step) * 10) / 10
        notify(qsTr("Subtitle delay %1").arg(delayText(player.subtitleDelay)))
    }

    function shiftAudio(step) {
        player.audioDelay = Math.round((player.audioDelay + step) * 10) / 10
        notify(qsTr("Audio delay %1").arg(delayText(player.audioDelay)))
    }

    // The small window needs the video on screen and a way to draw it twice:
    // not with HDR10 output, where the video lives under the interface layer.
    readonly property bool pipAvailable: !HdrOutput.active && player.active && !player.loading

    function enterPip() {
        if (!pipAvailable) return
        if (fullscreen) toggleFullscreen()
        player.enterPip()
        Navigator.back()
    }

    function leave() {
        if (fullscreen) toggleFullscreen()
        Navigator.back()
    }

    // With HDR10 output the video goes under the window's interface layer
    // (HdrScene); the page stays transparent over it.
    readonly property bool videoUnder: HdrOutput.active && HdrOutput.videoLayer !== null

    Component.onCompleted: player.attachVideo(videoUnder ? HdrOutput.videoLayer : videoHost, page)
    // Leaving for the picture-in-picture window keeps the file playing.
    Component.onDestruction: if (!player.pip) player.stop()

    Connections {
        target: page.player
        // Back to where playback started once the file ends on its own.
        function onStateChanged() {
            if (page.player.ended && !page.player.inGroup && !page.player.hasNext) page.leave()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: !page.videoUnder
    }

    Item {
        id: videoHost
        anchors.fill: parent
    }

    Timer {
        // Once a second while the display shows; the minute is what matters.
        interval: 1000
        running: page.osdShown
        repeat: true
        triggeredOnStart: true
        onTriggered: page.now = new Date()
    }

    Timer {
        id: osdTimer
        interval: 3000
        running: true
        onTriggered: page.osdVisible = false
    }

    MouseArea {
        objectName: "videoArea"
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: page.osdShown ? Qt.ArrowCursor : Qt.BlankCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPositionChanged: page.poke()
        onClicked: (mouse) => {
            page.forceActiveFocus()
            // The right button: the video's own menu, where the click was.
            if (mouse.button === Qt.RightButton) {
                page.openMenu = ""
                contextAnchor.x = mouse.x
                contextAnchor.y = mouse.y
                contextMenu.open()
                page.poke()
                return
            }
            // A click while the display is hidden only brings it back: it
            // was probably meant for a button that had faded out.
            if (page.openMenu.length > 0) page.openMenu = ""
            else if (page.osdShown) clickTimer.restart()
            page.poke()
        }
        // The first click of a double-click must not pause: the single
        // click waits until a double-click can no longer come.
        onDoubleClicked: (mouse) => {
            if (mouse.button !== Qt.LeftButton) return
            clickTimer.stop()
            page.toggleFullscreen()
        }
        Timer {
            id: clickTimer
            objectName: "clickTimer"
            interval: Qt.styleHints.mouseDoubleClickInterval
            onTriggered: page.player.togglePause()
        }
        Item {
            id: contextAnchor
            width: 1
            height: 1
        }
        PopupMenu {
            id: contextMenu
            objectName: "videoContextMenu"
            anchorItem: contextAnchor
            entries: [page.statsShown ? qsTr("Hide stats for nerds") : qsTr("Stats for nerds")]
            onActivated: page.statsShown = !page.statsShown
        }
    }

    Loader {
        id: statsLoader
        objectName: "statsOverlay"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.spacing + page.safeX
        anchors.topMargin: topBar.height + Theme.spacingSmall
        active: page.statsShown && page.player.active
        sourceComponent: StatsOverlay {
            player: page.player
            group: page.player.inGroup ? GroupSession : null
            onCloseRequested: page.statsShown = false
        }
    }

    Rectangle {
        objectName: "notice"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: topBar.height + Theme.spacingLarge
        width: noticeText.implicitWidth + 2 * Theme.spacing
        height: noticeText.implicitHeight + Theme.spacing
        radius: Theme.radius
        color: "#c0000000"
        visible: noticeTimer.running

        Text {
            id: noticeText
            anchors.centerIn: parent
            color: Overlay.text
            font.pixelSize: Theme.fontSizeLarge
        }
        Timer {
            id: holdTimer
            // Longer than a held key's or a gamepad's repeat.
            interval: 450
            onTriggered: page.commitSeek()
        }
        Timer {
            id: noticeTimer
            interval: 1200
        }
    }

    LoadingIndicator {
        anchors.centerIn: parent
        visible: page.player.loading
    }

    Text {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.spacingHuge, 640)
        visible: page.player.error.length > 0
        text: qsTr("Playback failed: %1").arg(page.player.error)
        color: Overlay.text
        font.pixelSize: Theme.fontSizeLarge
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
    }

    // External subtitles the server is still preparing, or could not give:
    // said near the top, where subtitles do not go.
    Rectangle {
        id: trackStatus
        objectName: "trackStatus"
        property bool failedShown: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: Theme.controlHeight + 2 * Theme.spacingLarge
        visible: page.player.subtitleLoading || failedShown
        width: trackStatusText.implicitWidth + 2 * Theme.spacingLarge
        height: trackStatusText.implicitHeight + Theme.spacing
        radius: height / 2
        color: Overlay.background
        Text {
            id: trackStatusText
            objectName: "trackStatusText"
            anchors.centerIn: parent
            text: page.player.subtitleLoading ? qsTr("Loading subtitles from the server…")
                                              : qsTr("The server could not give these subtitles")
            color: Overlay.text
            font.pixelSize: Theme.fontSizeBody
        }
        Timer {
            id: failedTimer
            interval: 6000
            onTriggered: trackStatus.failedShown = false
        }
        Connections {
            target: page.player
            function onTrackLoadFailed(subtitles) {
                if (!subtitles)
                    return
                trackStatus.failedShown = true
                failedTimer.restart()
            }
        }
    }

    // Top bar.
    Rectangle {
        id: topBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        // Tall and dark enough that the two-line clock stays readable over
        // bright video. "Ends at" sits about 40 to 55 px down, which is where
        // a letterboxed film's picture starts (16:9 in 1280x800): the shade
        // stays dark down to it and fades out only below.
        height: Theme.controlHeight + 3 * Theme.spacing + page.safeY
        opacity: page.osdShown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        gradient: Gradient {
            GradientStop { position: 0; color: "#d0000000" }
            GradientStop { position: 0.75; color: "#b0000000" }
            GradientStop { position: 1; color: "transparent" }
        }

        Row {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Theme.spacing + page.safeY
            height: Theme.controlHeight + Theme.spacing
            anchors.leftMargin: Theme.spacing + page.safeX
            anchors.rightMargin: Theme.spacing + page.safeX
            spacing: Theme.spacing

            // Back: a round button with a hairline edge.
            OsdButton {
                id: backButton
                objectName: "playerBack"
                iconSource: Icons.back
                toolTipText: qsTr("Back")
                onClicked: page.leave()
                background: Rectangle {
                    radius: width / 2
                    color: backButton.down || backButton.hovered ? Overlay.surfaceHover : Qt.alpha(Overlay.surface, 0.35)
                    border.width: 1
                    border.color: Overlay.border
                    FocusFrame { shown: backButton.visualFocus; radius: width / 2 }
                }
            }
            Column {
                anchors.verticalCenter: parent.verticalCenter
                // The clock's room only while it shows.
                width: Math.max(0, parent.width - Theme.controlHeight - groupOsd.width
                                   - (timeInfo.visible ? timeInfo.width + Theme.spacing : 0) - 3 * Theme.spacing)
                spacing: 2
                Text {
                    objectName: "playerTitle"
                    width: parent.width
                    text: page.player.live && page.player.channelName.length > 0
                          ? (page.player.channelNumber.length > 0 ? page.player.channelNumber + "  " : "")
                            + page.player.channelName
                          : page.mainTitle
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }
                // An episode: its season, number and name under the series.
                Text {
                    objectName: "playerSubtitle"
                    width: parent.width
                    visible: text.length > 0 && !page.player.live
                    text: page.episodeLine
                    color: Overlay.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                }
                // A channel: the programme on now, its times and progress.
                Row {
                    visible: page.player.live && page.player.programTitle.length > 0
                    width: parent.width
                    spacing: Theme.spacingSmall
                    Text {
                        id: programText
                        objectName: "programTitle"
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, parent.width - programBar.width - Theme.spacingSmall)
                        text: qsTr("%1–%2  %3").arg(page.shortTime(page.player.programStart))
                                               .arg(page.shortTime(page.player.programEnd))
                                               .arg(page.player.programTitle)
                        color: Overlay.text
                        opacity: 0.85
                        font.pixelSize: Theme.fontSizeSmall
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        id: programBar
                        objectName: "programProgress"
                        anchors.verticalCenter: parent.verticalCenter
                        width: 96
                        height: 3
                        radius: 1
                        color: Overlay.surfaceHover
                        Rectangle {
                            width: parent.width * Math.max(0, Math.min(1, page.player.programProgress))
                            height: parent.height
                            radius: parent.radius
                            color: Theme.accent
                        }
                    }
                }
            }
            // Only in a group: GroupSession exists once the group module
            // is installed.
            Loader {
                id: groupOsd
                anchors.verticalCenter: parent.verticalCenter
                active: page.player.inGroup
                sourceComponent: GroupOsd {}
            }
            // Time now and when the file ends, at the playback speed.
            Column {
                id: timeInfo
                objectName: "timeInfo"
                anchors.verticalCenter: parent.verticalCenter
                // The clock can be turned off in the settings.
                visible: page.player.showClock && page.width >= 560
                Text {
                    anchors.right: parent.right
                    text: page.shortTime(page.now)
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeBody
                    style: Text.Outline
                    styleColor: "#c0000000"
                }
                Text {
                    anchors.right: parent.right
                    visible: page.hasEnd
                    text: qsTr("Ends at %1").arg(page.shortTime(page.endsAt))
                    color: Overlay.text
                    opacity: 0.85
                    font.pixelSize: Theme.fontSizeSmall
                    style: Text.Outline
                    styleColor: "#c0000000"
                }
            }
        }
    }

    // Skip the intro or the like: on the right over the bar, with a line
    // showing how much of the part has played.
    ActionButton {
        id: skipButton
        objectName: "skipSegment"
        anchors.right: parent.right
        anchors.bottom: bottomBar.top
        anchors.margins: Theme.spacingLarge
        anchors.rightMargin: Theme.spacingLarge + page.safeX
        primary: true
        visible: page.player.segment.length > 0 && page.openMenu !== "episodes"
        iconSource: PlayerIcons.forward
        text: page.player.segment === "Intro" ? qsTr("Skip intro")
            : page.player.segment === "Outro" ? qsTr("Skip credits")
            : page.player.segment === "Recap" ? qsTr("Skip recap")
            : page.player.segment === "Preview" ? qsTr("Skip preview")
            : qsTr("Skip")
        rightPadding: Theme.spacingLarge + skipProgress.width + Theme.spacing
        onClicked: page.player.skipSegment()

        // The part playing now and how far into it.
        readonly property var part: {
            const at = page.player.position
            for (const segment of page.player.segments)
                if (segment.type === page.player.segment && at >= segment.start && at < segment.end)
                    return segment
            return null
        }
        Rectangle {
            id: skipProgress
            objectName: "skipProgress"
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLarge
            anchors.verticalCenter: parent.verticalCenter
            width: 40
            height: 3
            radius: 1.5
            color: Qt.alpha(Overlay.text, 0.25)
            Rectangle {
                height: parent.height
                radius: parent.radius
                color: Theme.accent
                width: skipButton.part ? parent.width * Math.max(0, Math.min(1,
                    (page.player.position - skipButton.part.start) / Math.max(0.1, skipButton.part.end - skipButton.part.start))) : 0
            }
        }
    }

    // Menus open just above the row of buttons, on the right.
    Loader {
        id: menuLoader
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: Theme.spacing + page.safeX
        // Above the whole bar, seek line included.
        anchors.bottomMargin: bottomBar.height + Theme.spacingSmall
        active: page.openMenu.length > 0 && page.openMenu !== "episodes"
        sourceComponent: page.openMenu === "tune" ? tuneMenu
            : page.openMenu === "segments" ? segmentsMenu : trackMenu
    }
    // The episodes: a sheet along the bottom, in place of the bar.
    Loader {
        id: sheetLoader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: page.safeY
        active: page.openMenu === "episodes"
        sourceComponent: episodesMenu
    }
    // Audio and subtitles together, opened as either.
    Component {
        id: trackMenu
        TracksMenu {
            availableWidth: page.width - 2 * (Theme.spacing + page.safeX)
            audioTracks: page.player.audioTracks
            subtitleTracks: page.player.subtitleTracks
            audioIndex: page.player.audioIndex
            subtitleIndex: page.player.subtitleIndex
            focusSubtitles: page.openMenu === "subtitles"
            onAudioChosen: (streamIndex) => {
                page.player.selectAudio(streamIndex)
                page.openMenu = ""
            }
            onSubtitleChosen: (streamIndex) => {
                page.player.selectSubtitle(streamIndex)
                page.openMenu = ""
            }
            onTuneRequested: {
                page.tunePage = "subtitles"
                page.toggleMenu("tune")
            }
        }
    }
    Component {
        id: episodesMenu
        EpisodesSheet {
            episodes: page.player.episodes
            onChosen: (itemId) => {
                page.playEpisode(itemId)
                page.openMenu = ""
            }
            onCloseRequested: page.openMenu = ""
        }
    }
    Component {
        id: segmentsMenu
        SegmentsMenu {
            segments: page.player.segments
            position: page.player.position
            onChosen: (start) => {
                page.player.seek(start)
                page.openMenu = ""
            }
        }
    }
    Component {
        id: tuneMenu
        TuneMenu {
            speedAvailable: !page.player.inGroup || GroupSession.rateSupported
            statsShown: page.statsShown
            // Once, at the start: the menu goes on from there.
            Component.onCompleted: {
                section = page.tunePage
                page.tunePage = ""
            }
            onStatsToggled: {
                page.statsShown = !page.statsShown
                page.openMenu = ""
            }
        }
    }

    // Bottom bar.
    Rectangle {
        id: bottomBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: column.implicitHeight + 2 * Theme.spacing + page.safeY
        opacity: page.osdShown && page.openMenu !== "episodes" ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: "#d0000000" }
        }

        // The thumbnail over the seek bar while it is hovered or dragged.
        Rectangle {
            id: preview
            objectName: "trickplayPreview"

            readonly property real fraction: timeline.pressed
                ? timeline.position
                : Math.max(0, Math.min(1, (timelineHover.point.position.x - timeline.leftPadding)
                                           / Math.max(1, timeline.availableWidth)))
            readonly property real seconds: fraction * page.player.duration
            readonly property var frame: page.player.hasTrickplay && (timeline.pressed || timelineHover.hovered)
                ? page.player.trickplayFrame(seconds) : ({})
            // Thumbnails at most this wide on screen.
            readonly property real scaleFactor: frame.width ? Math.min(1, 240 / frame.width) : 1

            visible: frame.source !== undefined
            width: (frame.width || 0) * scaleFactor + 2 * border.width
            height: (frame.height || 0) * scaleFactor + 2 * border.width + label.height
            x: Math.max(0, Math.min(bottomBar.width - width,
                                    column.x + timeline.x + timeline.leftPadding
                                    + fraction * timeline.availableWidth - width / 2))
            anchors.bottom: column.top
            anchors.bottomMargin: Theme.spacingSmall
            color: "black"
            border.color: Overlay.surfaceHover
            border.width: 1
            radius: 4

            Item {
                x: preview.border.width
                y: preview.border.width
                width: (preview.frame.width || 0) * preview.scaleFactor
                height: (preview.frame.height || 0) * preview.scaleFactor
                clip: true

                Image {
                    x: -(preview.frame.x || 0) * preview.scaleFactor
                    y: -(preview.frame.y || 0) * preview.scaleFactor
                    width: (preview.frame.tileWidth || 0) * preview.scaleFactor
                    height: (preview.frame.tileHeight || 0) * preview.scaleFactor
                    source: preview.frame.source || ""
                    sourceSize: Qt.size(preview.frame.tileWidth || 0, preview.frame.tileHeight || 0)
                    asynchronous: true
                    // The previous tile stays until the next one arrives.
                    retainWhileLoading: true
                    smooth: true
                }
            }

            Text {
                id: label
                anchors.bottom: parent.bottom
                anchors.horizontalCenter: parent.horizontalCenter
                readonly property string chapter: page.player.chapters.length > 0 ? page.player.chapterAt(preview.seconds) : ""
                text: page.clock(preview.seconds) + (chapter.length > 0 ? "  ·  " + chapter : "")
                color: Overlay.text
                font.pixelSize: Theme.fontSizeBody
                padding: 2
            }
        }

        Column {
            id: column
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.margins: Theme.spacing
            anchors.leftMargin: Theme.spacing + page.safeX
            anchors.rightMargin: Theme.spacing + page.safeX
            anchors.bottomMargin: Theme.spacing + page.safeY
            spacing: Theme.spacingSmall

            ValueSlider {
                id: timeline
                objectName: "timeline"
                width: parent.width
                from: 0
                to: Math.max(page.player.duration, 1)
                // A live channel has no timeline.
                visible: !page.player.live
                stepSize: 10
                focusPolicy: Qt.TabFocus
                // Larger in TV mode, read from across the room.
                trackHeight: page.tvMode ? 10 : 4
                handleSize: page.tvMode ? 32 : 18
                // Follows playback except while dragged, or while a held
                // key in TV mode chooses where to go.
                Binding on value {
                    when: !timeline.pressed
                    value: page.seekPending ? page.seekTarget : page.player.position
                }
                // TV mode: the page's seek, faster when held.
                Keys.onLeftPressed: (event) => {
                    if (!page.tvMode) { event.accepted = false; return }
                    page.seekStep(-1, event.isAutoRepeat)
                    page.poke()
                }
                Keys.onRightPressed: (event) => {
                    if (!page.tvMode) { event.accepted = false; return }
                    page.seekStep(1, event.isAutoRepeat)
                    page.poke()
                }
                // A drag seeks once, on release; a key step (Tab focus) at
                // once.
                onMoved: {
                    page.poke()
                    if (!pressed) page.player.seek(value)
                }
                onPressedChanged: if (!pressed) page.player.seek(value)

                HoverHandler {
                    id: timelineHover
                }

                // Where the intro, credits and the like are: a bright band
                // on the line, over the track and under the handle.
                Repeater {
                    model: page.player.duration > 0 ? page.player.segments : []
                    delegate: Rectangle {
                        required property var modelData
                        objectName: "segmentMark"
                        z: -0.5
                        x: timeline.leftPadding + modelData.start / page.player.duration * timeline.availableWidth
                        width: Math.max(3, (modelData.end - modelData.start) / page.player.duration * timeline.availableWidth)
                        y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                        height: 8
                        radius: 2
                        color: modelData.type === "Intro" || modelData.type === "Recap" || modelData.type === "Preview"
                            ? Theme.accent : Overlay.text
                        opacity: 0.75
                    }
                }

                // Where each chapter starts (the first, at the start, has none).
                Repeater {
                    model: page.player.duration > 0 ? page.player.chapters : []
                    delegate: Rectangle {
                        required property var modelData
                        required property int index
                        objectName: "chapterMark"
                        visible: index > 0 && modelData.start < page.player.duration
                        z: -0.4
                        x: timeline.leftPadding + modelData.start / page.player.duration * timeline.availableWidth - width / 2
                        width: 2
                        height: timeline.trackHeight + 8
                        y: timeline.topPadding + timeline.availableHeight / 2 - height / 2
                        radius: 1
                        color: Overlay.text
                        opacity: 0.7
                    }
                }
            }

            // Elapsed on the left, what is left on the right.
            Item {
                width: parent.width
                height: timeText.implicitHeight
                Text {
                    id: timeText
                    objectName: "timeText"
                    anchors.left: parent.left
                    text: page.player.live ? qsTr("Live") : page.clock(timeline.value)
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.Medium
                    style: Text.Outline
                    styleColor: "#60000000"
                }
                Text {
                    objectName: "remainingText"
                    anchors.right: parent.right
                    visible: page.hasEnd
                    text: "−" + page.clock(Math.max(0, page.player.duration - timeline.value))
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeBody
                    font.weight: Font.Medium
                    style: Text.Outline
                    styleColor: "#60000000"
                }
            }

            Item {
                width: parent.width
                height: playPauseButton.height

                Row {
                    id: leftControls
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSmall

                    OsdButton {
                        objectName: "previous"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !page.player.live && !page.player.inGroup && page.player.hasPrevious
                        iconSource: PlayerIcons.previous
                        toolTipText: qsTr("Previous")
                        onClicked: page.player.previous()
                    }
                    // Play and pause: the big round light button.
                    OsdButton {
                        id: playPauseButton
                        objectName: "playPause"
                        anchors.verticalCenter: parent.verticalCenter
                        implicitWidth: Math.round(Theme.controlHeight * 1.4)
                        implicitHeight: implicitWidth
                        iconSource: page.player.paused ? Icons.play : PlayerIcons.pause
                        toolTipText: page.player.paused ? qsTr("Play") : qsTr("Pause")
                        onClicked: page.player.togglePause()
                        contentItem: Item {
                            Icon {
                                anchors.centerIn: parent
                                source: playPauseButton.iconSource
                                color: "#0b0e15"
                                size: Theme.iconSize + 4
                            }
                        }
                        background: Rectangle {
                            radius: width / 2
                            color: playPauseButton.down ? "#d8dce4" : playPauseButton.hovered ? "#ffffff" : "#f2f4f8"
                            FocusFrame { shown: playPauseButton.visualFocus; radius: width / 2 }
                        }
                    }
                    OsdButton {
                        objectName: "channelDown"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.player.live && page.player.canChangeChannel
                        iconSource: PlayerIcons.previous
                        toolTipText: qsTr("Previous channel")
                        onClicked: page.changeChannel(-1)
                    }
                    OsdButton {
                        objectName: "channelUp"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.player.live && page.player.canChangeChannel
                        iconSource: PlayerIcons.next
                        toolTipText: qsTr("Next channel")
                        onClicked: page.changeChannel(1)
                    }
                    OsdButton {
                        objectName: "next"
                        anchors.verticalCenter: parent.verticalCenter
                        // A group's queue is the server's; alone, the series'.
                        visible: !page.player.live && (page.player.inGroup || page.player.hasNext)
                        iconSource: PlayerIcons.next
                        toolTipText: qsTr("Next")
                        onClicked: page.player.next()
                    }
                    SpeedButton {
                        objectName: "back10"
                        anchors.verticalCenter: parent.verticalCenter
                        focusPolicy: Qt.TabFocus
                        // A live channel cannot be sought.
                        visible: page.width >= 720 && !page.player.live
                        text: "−10"
                        toolTipText: qsTr("Back 10 seconds")
                        onClicked: page.player.seekBy(-10)
                    }
                    SpeedButton {
                        objectName: "forward10"
                        anchors.verticalCenter: parent.verticalCenter
                        focusPolicy: Qt.TabFocus
                        visible: page.width >= 720 && !page.player.live
                        text: "+10"
                        toolTipText: qsTr("Forward 10 seconds")
                        onClicked: page.player.seekBy(10)
                    }
                    OsdButton {
                        objectName: "mute"
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: page.player.muted || page.player.volume === 0 ? PlayerIcons.volumeOff
                                                                                  : PlayerIcons.volume
                        toolTipText: page.player.muted ? qsTr("Unmute") : qsTr("Mute")
                        onClicked: page.toggleMute()
                    }
                    ValueSlider {
                        objectName: "volume"
                        focusPolicy: Qt.TabFocus
                        anchors.verticalCenter: parent.verticalCenter
                        // The mute button stays when the window is narrow.
                        visible: page.width >= 800
                        width: 110
                        from: 0
                        to: 100
                        stepSize: 1
                        value: page.player.muted ? 0 : page.player.volume
                        onMoved: {
                            page.player.muted = false
                            page.player.volume = Math.round(value)
                            page.poke()
                        }
                    }
                    // What part is playing: the segment, else the chapter
                    // and its place among them.
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        leftPadding: Theme.spacingSmall
                        spacing: Theme.spacingSmall
                        visible: partText.part.length > 0
                                 && rightControls.x - leftControls.x - x >= partText.width + partCount.width + 2 * Theme.spacing
                        Text {
                            id: partText
                            objectName: "partText"
                            anchors.verticalCenter: parent.verticalCenter
                            readonly property string part: page.player.segment.length > 0
                                ? page.segmentName(page.player.segment)
                                : page.player.chapters.length > 0 ? page.player.chapterAt(timeline.value) : ""
                            text: part
                            color: Overlay.text
                            font.pixelSize: Theme.fontSizeBody
                            font.weight: Theme.fontWeightBold
                            elide: Text.ElideRight
                            width: Math.min(implicitWidth, 300)
                        }
                        Text {
                            id: partCount
                            objectName: "partCount"
                            anchors.verticalCenter: parent.verticalCenter
                            visible: page.player.segment.length === 0 && page.player.chapters.length > 1
                                     && page.chapterIndex >= 0
                            text: "· " + (page.chapterIndex + 1) + "/" + page.player.chapters.length
                            color: Overlay.textSecondary
                            font.pixelSize: Theme.fontSizeBody
                        }
                    }
                }

                Row {
                    id: rightControls
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.spacingSmall
                    // Names beside the icons while there is room.
                    readonly property bool labels: page.width >= 1100

                    SpeedButton {
                        objectName: "tracksButton"
                        focusPolicy: Qt.TabFocus
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !page.tvMode
                        iconSource: PlayerIcons.subtitles
                        // The subtitles' language at a glance.
                        text: rightControls.labels ? page.subtitleLabel : ""
                        toolTipText: qsTr("Audio and subtitles")
                        onClicked: page.toggleMenu(page.openMenu === "audio" ? "audio" : "subtitles")
                    }
                    SpeedButton {
                        objectName: "speedButton"
                        focusPolicy: Qt.TabFocus
                        // The speed at a glance; in a group it is everyone's.
                        visible: page.width >= 720 && (!page.player.inGroup || GroupSession.rateSupported)
                        anchors.verticalCenter: parent.verticalCenter
                        text: page.player.baseRate + "×"
                        changed: Math.abs(page.player.baseRate - 1) > 0.001
                        toolTipText: qsTr("Speed")
                        onClicked: page.toggleMenu("tune")
                    }
                    OsdButton {
                        objectName: "segmentsButton"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.player.segments.length > 0
                        iconSource: PlayerIcons.segments
                        toolTipText: qsTr("Segments")
                        onClicked: page.toggleMenu("segments")
                    }
                    SpeedButton {
                        objectName: "episodes"
                        focusPolicy: Qt.TabFocus
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.hasEpisodes && !page.tvMode
                        iconSource: PlayerIcons.episodes
                        text: rightControls.labels ? qsTr("Episodes") : ""
                        toolTipText: qsTr("Episodes")
                        onClicked: page.toggleMenu("episodes")
                    }
                    OsdButton {
                        objectName: "tuneButton"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: !page.tvMode
                        iconSource: Icons.settings
                        toolTipText: qsTr("Player settings")
                        onClicked: page.toggleMenu("tune")
                    }
                    OsdButton {
                        objectName: "pip"
                        anchors.verticalCenter: parent.verticalCenter
                        visible: page.pipAvailable
                        iconSource: PlayerIcons.pip
                        toolTipText: qsTr("Picture in picture")
                        onClicked: page.enterPip()
                    }
                    OsdButton {
                        anchors.verticalCenter: parent.verticalCenter
                        iconSource: page.fullscreen ? PlayerIcons.fullscreenExit : PlayerIcons.fullscreen
                        toolTipText: page.fullscreen ? qsTr("Exit fullscreen") : qsTr("Fullscreen")
                        onClicked: page.toggleFullscreen()
                    }
                }
            }

            // TV mode: the menus as one row of named buttons.
            Row {
                objectName: "tvMenus"
                visible: page.tvMode
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.spacing

                ActionButton {
                    objectName: "tvEpisodes"
                    visible: page.hasEpisodes
                    focusPolicy: Qt.TabFocus
                    text: qsTr("Episodes")
                    onClicked: page.toggleMenu("episodes")
                }
                ActionButton {
                    objectName: "tvSubtitles"
                    focusPolicy: Qt.TabFocus
                    text: qsTr("Subtitles")
                    onClicked: page.toggleMenu("subtitles")
                }
                ActionButton {
                    objectName: "tvAudio"
                    focusPolicy: Qt.TabFocus
                    text: qsTr("Audio")
                    onClicked: page.toggleMenu("audio")
                }
                ActionButton {
                    objectName: "tvSettings"
                    focusPolicy: Qt.TabFocus
                    text: qsTr("Settings")
                    onClicked: page.toggleMenu("tune")
                }
            }
        }
    }

    Keys.onPressed: (event) => {
        // Ctrl+K (the command palette) and the like are not player keys.
        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier)) {
            event.accepted = false
            return
        }
        if (page.tvMode) {
            const arrow = event.key === Qt.Key_Left || event.key === Qt.Key_Right
                || event.key === Qt.Key_Up || event.key === Qt.Key_Down
            if (arrow && page.osdFocused()) {
                // The shell moves the focus between the display's controls.
                page.poke()
                event.accepted = false
                return
            }
            if (page.tvKey(event)) {
                event.accepted = true
                return
            }
        }
        switch (event.key) {
        case Qt.Key_Space:
        case Qt.Key_K:
            page.player.togglePause(); break
        case Qt.Key_P:
            page.enterPip(); break
        case Qt.Key_Left:
            page.player.seekBy(-10); break
        case Qt.Key_Right:
            page.player.seekBy(10); break
        case Qt.Key_Up:
            page.changeVolume(5); break
        case Qt.Key_Down:
            page.changeVolume(-5); break
        case Qt.Key_M:
            page.toggleMute(); break
        case Qt.Key_J:
            if (event.modifiers & Qt.ShiftModifier) page.shiftAudio(-0.1)
            else page.shiftSubtitles(-0.1)
            break
        case Qt.Key_L:
            if (event.modifiers & Qt.ShiftModifier) page.shiftAudio(0.1)
            else page.shiftSubtitles(0.1)
            break
        case Qt.Key_F:
            page.toggleFullscreen(); break
        case Qt.Key_S:
            page.player.skipSegment(); break
        case Qt.Key_BracketRight:
            page.player.jumpToSegment(1); break
        case Qt.Key_BracketLeft:
            page.player.jumpToSegment(-1); break
        case Qt.Key_N:
            if (page.player.live) page.changeChannel(1)
            else page.player.next()
            break
        case Qt.Key_PageUp:
        case Qt.Key_ChannelUp:
            if (page.openMenu === "episodes" && sheetLoader.item) {
                sheetLoader.item.stepSeason(-1)
                break
            }
            if (!page.player.live) return
            page.changeChannel(1); break
        case Qt.Key_PageDown:
        case Qt.Key_ChannelDown:
            if (page.openMenu === "episodes" && sheetLoader.item) {
                sheetLoader.item.stepSeason(1)
                break
            }
            if (!page.player.live) return
            page.changeChannel(-1); break
        case Qt.Key_E:
            if (!page.hasEpisodes) return
            page.toggleMenu("episodes"); break
        case Qt.Key_A:
            page.toggleMenu("audio"); break
        case Qt.Key_C:
            page.toggleMenu("subtitles"); break
        case Qt.Key_I:
            page.statsShown = !page.statsShown; break
        case Qt.Key_Escape:
        case Qt.Key_Back:
            if (page.openMenu.length > 0) page.openMenu = ""
            else if (page.fullscreen) page.toggleFullscreen()
            else page.leave()
            break
        default:
            return
        }
        page.poke()
        event.accepted = true
    }
    // A held Left or Right let go: its one seek.
    Keys.onReleased: (event) => {
        if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right) && !event.isAutoRepeat && page.seekPending)
            page.commitSeek()
        event.accepted = false
    }
}
