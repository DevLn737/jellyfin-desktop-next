pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The series' episodes as a sheet along the bottom of the player: the
// season tabs (Page Up and Down, a gamepad's LB and RB), then a strip of
// cards: the still with the number (or "Now" and "Next"), the watched check
// and the progress, "N. Name", the run time and the start of the overview.
// The season that plays opens at the episode after it. `episodes` are
// PlayerSession episode entries; emits the chosen item id.
Rectangle {
    id: sheet

    property var episodes: []

    signal chosen(string itemId)
    signal closeRequested()

    // Season numbers in the list's order, with their episode counts.
    readonly property var seasons: {
        const out = []
        for (let i = 0; i < episodes.length; ++i) {
            const season = episodes[i].season || 0
            if (out.indexOf(season) < 0) out.push(season)
        }
        return out
    }
    readonly property bool hasTabs: seasons.length > 1
    // The season shown: the playing episode's, until a tab is chosen.
    property int chosenSeason: -1
    readonly property int season: chosenSeason >= 0 ? chosenSeason : currentSeason()
    readonly property var shown: hasTabs ? episodes.filter(e => (e.season || 0) === season) : episodes
    readonly property int watched: episodes.filter(e => e.played === true).length
    // The card the strip opens at: the one after the episode that plays.
    readonly property int nextRow: {
        const current = currentRow()
        return current >= 0 && current + 1 < shown.length ? current + 1 : -1
    }

    function currentSeason() {
        for (let i = 0; i < episodes.length; ++i)
            if (episodes[i].current) return episodes[i].season || 0
        return seasons.length > 0 ? seasons[0] : 0
    }
    function count(season) {
        return episodes.filter(e => (e.season || 0) === season).length
    }
    function seasonName(season) {
        return season > 0 ? qsTr("Season %1").arg(season) : qsTr("Specials")
    }
    function runTime(ticks) {
        const minutes = Math.round((ticks || 0) / 600000000)
        return minutes > 0 ? qsTr("%1 min").arg(minutes) : ""
    }
    function currentRow() {
        for (let i = 0; i < shown.length; ++i)
            if (shown[i].current) return i
        return -1
    }
    // LB and RB: the season before or after.
    function stepSeason(step) {
        const at = seasons.indexOf(season)
        const to = at + step
        if (to < 0 || to >= seasons.length) return
        chosenSeason = seasons[to]
        Qt.callLater(focusFirst)
    }

    // Keys and the TV mode: the episode after the one that plays, else the
    // first that can be chosen.
    function focusFirst() {
        const start = nextRow >= 0 ? nextRow : Math.max(0, currentRow())
        strip.positionViewAtIndex(Math.max(0, start - (start > 0 ? 1 : 0)), ListView.Beginning)
        strip.currentIndex = start
        for (let i = start; i < shown.length; ++i) {
            const card = strip.itemAtIndex(i)
            if (card && card.enabled) {
                strip.currentIndex = i
                card.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
        if (tabs.visible) tabs.forceActiveFocus(Qt.TabFocusReason)
    }

    objectName: "episodesSheet"
    readonly property real cardWidth: Theme.tvMode ? 340 : 300
    height: header.height + strip.height + 3 * Theme.spacingLarge
    color: Qt.alpha(Overlay.background, 0.97)

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_PageUp) sheet.stepSeason(-1)
        else if (event.key === Qt.Key_PageDown) sheet.stepSeason(1)
        else return
        event.accepted = true
    }

    // A soft edge above the sheet.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.top
        height: 80
        gradient: Gradient {
            GradientStop { position: 0; color: "transparent" }
            GradientStop { position: 1; color: sheet.color }
        }
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingLarge
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        height: Math.max(title.implicitHeight, tabsFrame.height)

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingLarge

            Text {
                id: title
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Episodes")
                color: Overlay.text
                font.pixelSize: Theme.fontSizeTitle
                font.weight: Font.ExtraBold
            }
            // The seasons, in a frame.
            Rectangle {
                id: tabsFrame
                anchors.verticalCenter: parent.verticalCenter
                visible: sheet.hasTabs
                width: Math.min(tabs.contentWidth + 8, header.width - title.width - watchedText.width - 4 * Theme.spacingLarge)
                height: Theme.controlHeight + 8
                radius: Theme.radius + 2
                color: Qt.alpha(Overlay.surface, 0.6)
                border.width: 1
                border.color: Overlay.border

                ListView {
                    id: tabs
                    objectName: "episodeSeasonTabs"
                    anchors.fill: parent
                    anchors.margins: 4
                    orientation: ListView.Horizontal
                    spacing: 2
                    clip: true
                    keyNavigationEnabled: true
                    activeFocusOnTab: true
                    boundsBehavior: Flickable.StopAtBounds
                    model: sheet.seasons
                    KeyNavigation.down: strip
                    delegate: T.AbstractButton {
                        id: tab
                        required property int modelData
                        objectName: "seasonTab_" + modelData
                        readonly property bool selected: modelData === sheet.season
                        height: tabs.height
                        implicitWidth: tabRow.implicitWidth + 2 * Theme.spacing
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        Accessible.name: tabText.text
                        Accessible.role: Accessible.PageTab
                        Keys.onReturnPressed: tab.clicked()
                        Keys.onEnterPressed: tab.clicked()
                        onClicked: {
                            sheet.chosenSeason = modelData
                            strip.positionViewAtBeginning()
                        }
                        contentItem: Item {
                            Row {
                                id: tabRow
                                anchors.centerIn: parent
                                spacing: Theme.spacingSmall
                                Text {
                                    id: tabText
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: sheet.seasonName(tab.modelData)
                                    color: tab.selected || tab.hovered ? Overlay.text : Overlay.textSecondary
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: tab.selected ? Theme.fontWeightBold : Font.Medium
                                }
                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: sheet.count(tab.modelData)
                                    color: Overlay.textSecondary
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radius
                            color: tab.selected ? Overlay.surfaceHover : "transparent"
                            FocusFrame { shown: tab.visualFocus }
                        }
                    }
                }
            }
        }

        Row {
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSmall

            // The gamepad's keys for the sheet, while one is in use.
            Row {
                objectName: "episodesHints"
                anchors.verticalCenter: parent.verticalCenter
                visible: Theme.focusDriven
                spacing: Theme.spacingSmall
                KeyCap { text: "LB" }
                KeyCap { text: "RB" }
                Hint { text: qsTr("season") + "  ·" }
                KeyCap { text: "B" }
                Hint { text: qsTr("close") }
            }
            Text {
                id: watchedText
                objectName: "episodesWatched"
                anchors.verticalCenter: parent.verticalCenter
                visible: sheet.episodes.length > 0 && !Theme.tvMode
                text: qsTr("%1 of %2 watched").arg(sheet.watched).arg(sheet.episodes.length)
                color: Overlay.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            IconButton {
                objectName: "episodesClose"
                anchors.verticalCenter: parent.verticalCenter
                visible: !Theme.tvMode
                focusPolicy: Qt.TabFocus
                iconSource: Icons.close
                toolTipText: qsTr("Close")
                onClicked: sheet.closeRequested()
            }
        }
    }

    ListView {
        id: strip
        objectName: "episodeStrip"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.topMargin: Theme.spacingLarge
        leftMargin: Theme.pageMargin
        rightMargin: Theme.pageMargin
        height: Math.round(sheet.cardWidth * 9 / 16) + 96
        orientation: ListView.Horizontal
        spacing: Theme.spacing + 4
        clip: true
        keyNavigationEnabled: true
        highlightFollowsCurrentItem: false
        boundsBehavior: Flickable.StopAtBounds
        model: sheet.shown
        KeyNavigation.up: tabs.visible ? tabs : null
        Component.onCompleted: Qt.callLater(() => {
            const start = sheet.nextRow >= 0 ? sheet.nextRow : Math.max(0, sheet.currentRow())
            strip.positionViewAtIndex(Math.max(0, start - (start > 0 ? 1 : 0)), ListView.Beginning)
        })
        SmoothWheel {}

        delegate: T.ItemDelegate {
            id: card
            required property var modelData
            required property int index
            readonly property bool current: modelData.current === true
            readonly property bool next: index === sheet.nextRow
            objectName: "episodeRow"
            width: sheet.cardWidth
            height: strip.height
            padding: 0
            focusPolicy: Qt.StrongFocus
            hoverEnabled: true
            // The one that plays stays where it is.
            enabled: !current
            Accessible.name: modelData.name || ""
            Keys.onReturnPressed: card.clicked()
            Keys.onEnterPressed: card.clicked()
            onActiveFocusChanged: if (activeFocus) strip.currentIndex = index
            onClicked: sheet.chosen(modelData.id)

            background: Item {}
            contentItem: Column {
                spacing: Theme.spacingSmall

                Rectangle {
                    id: still
                    objectName: "episodeStill"
                    width: card.width
                    height: Math.round(width * 9 / 16)
                    radius: Theme.radius
                    color: Overlay.surfaceRaised
                    clip: true

                    Image {
                        anchors.fill: parent
                        source: card.modelData.imageSource || ""
                        sourceSize.width: still.width * 2
                        fillMode: Image.PreserveAspectCrop
                        asynchronous: true
                        opacity: card.hovered || card.visualFocus ? 1 : 0.92
                    }
                    // The number, or Now and Next.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.margins: Theme.spacingSmall
                        width: tag.implicitWidth + 2 * Theme.spacingSmall
                        height: tag.implicitHeight + 6
                        radius: 6
                        visible: tag.text.length > 0
                        color: card.current ? Overlay.accent : Qt.alpha("#0b0e15", 0.82)
                        Text {
                            id: tag
                            anchors.centerIn: parent
                            text: card.current ? qsTr("Now")
                                : card.next ? qsTr("Next")
                                : card.modelData.number > 0 ? card.modelData.number.toString() : ""
                            color: card.current ? Overlay.accentText : "white"
                            font.pixelSize: Theme.fontSizeSmall
                            font.weight: Font.ExtraBold
                        }
                    }
                    Rectangle {
                        objectName: "episodeWatched"
                        visible: card.modelData.played === true
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: Theme.spacingSmall
                        width: 26
                        height: 26
                        radius: 13
                        color: Qt.alpha("#0b0e15", 0.82)
                        Icon {
                            anchors.centerIn: parent
                            source: Icons.check
                            color: "white"
                            size: 16
                        }
                    }
                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 4
                        visible: progressBar.visible || card.modelData.played === true
                        color: Qt.alpha("white", 0.25)
                    }
                    Rectangle {
                        id: progressBar
                        objectName: "episodeProgress"
                        visible: (card.modelData.progress || 0) > 0 && card.modelData.played !== true
                        anchors.left: parent.left
                        anchors.bottom: parent.bottom
                        width: parent.width * (card.modelData.progress || 0)
                        height: 4
                        color: Overlay.progress
                    }
                    // Watched: the line full.
                    Rectangle {
                        visible: card.modelData.played === true
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 4
                        color: "white"
                    }
                    // The focus: a light ring inside the still.
                    Rectangle {
                        anchors.fill: parent
                        radius: parent.radius
                        color: "transparent"
                        border.width: card.visualFocus ? 3 : card.hovered ? 2 : 0
                        border.color: "white"
                    }
                }

                Text {
                    width: card.width
                    text: (card.modelData.number > 0 ? card.modelData.number + ". " : "") + (card.modelData.name || "")
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeBody + 1
                    font.weight: Font.ExtraBold
                    elide: Text.ElideRight
                }
                Text {
                    width: card.width
                    text: {
                        const parts = []
                        const time = sheet.runTime(card.modelData.runTimeTicks)
                        if (time.length > 0) parts.push(time)
                        if ((card.modelData.overview || "").length > 0) parts.push(card.modelData.overview)
                        return parts.join(" · ")
                    }
                    visible: text.length > 0
                    color: Overlay.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    lineHeight: 1.15
                }
            }
        }
    }

    component KeyCap: Rectangle {
        property alias text: capText.text
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        width: Math.max(height, capText.implicitWidth + 12)
        height: 24
        radius: 6
        color: "transparent"
        border.width: 1
        border.color: Overlay.border
        Text {
            id: capText
            anchors.centerIn: parent
            color: Overlay.textSecondary
            font.pixelSize: Theme.fontSizeSmall - 1
            font.weight: Theme.fontWeightBold
        }
    }
    component Hint: Text {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        color: Overlay.textSecondary
        font.pixelSize: Theme.fontSizeSmall
    }
}
