pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls

// The series' episodes, in order, as on the series page: a still with the
// watched check and the progress, the number, the name and the run time.
// Several seasons get tabs; the one that plays opens. `episodes` are
// PlayerSession episode entries; emits the chosen item id.
Rectangle {
    id: panel

    property var episodes: []

    signal chosen(string itemId)

    // The room the page has for it (between the player's bars).
    property real maximumHeight: 420

    // Season numbers in the list's order.
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

    function currentSeason() {
        for (let i = 0; i < episodes.length; ++i) {
            if (episodes[i].current) return episodes[i].season || 0
        }
        return seasons.length > 0 ? seasons[0] : 0
    }

    function seasonName(season) {
        return season > 0 ? qsTr("Season %1").arg(season) : qsTr("Specials")
    }

    function code(episode) {
        return episode.number > 0 ? qsTr("E%1").arg(episode.number) : ""
    }

    function runTime(ticks) {
        const minutes = Math.round((ticks || 0) / 600000000)
        return minutes > 0 ? qsTr("%1 min").arg(minutes) : ""
    }

    function currentRow() {
        for (let i = 0; i < shown.length; ++i) {
            if (shown[i].current) return i
        }
        return -1
    }

    // Keys and the TV mode: the episode after the one that plays.
    function focusFirst() {
        const current = currentRow()
        const order = []
        for (let i = current + 1; i < shown.length; ++i) order.push(i)
        for (let i = 0; i < Math.max(0, current); ++i) order.push(i)
        for (const i of order) {
            const row = rows.itemAt(i)
            if (row && row.enabled) {
                row.forceActiveFocus(Qt.TabFocusReason)
                flick.reveal(row)
                return
            }
        }
        if (tabs.visible) tabs.forceActiveFocus(Qt.TabFocusReason)
    }

    width: Theme.tvMode ? 520 : 400
    height: Math.max(Theme.controlHeight * 2,
                     Math.min(column.implicitHeight + 2 * Theme.spacing, 560, maximumHeight))
    radius: Theme.radius
    color: Overlay.surface
    border.color: Overlay.border

    Flickable {
        SmoothWheel {}
        id: flick
        anchors.fill: parent
        anchors.margins: Theme.spacing
        contentHeight: column.implicitHeight
        clip: true

        // Opens scrolled to the episode that plays, in the middle; the
        // heading and the real row heights count.
        function scrollToCurrent() {
            const row = rows.itemAt(panel.currentRow())
            if (!row) return
            contentY = Math.max(0, Math.min(contentHeight - height, row.y + list.y - (height - row.height) / 2))
        }
        function reveal(row) {
            const top = row.y + list.y
            if (top < contentY) contentY = top
            else if (top + row.height > contentY + height) contentY = top + row.height - height
        }
        Component.onCompleted: Qt.callLater(scrollToCurrent)

        Column {
            id: column
            width: parent.width
            spacing: Theme.spacingSmall

            Item {
                width: parent.width
                height: title.implicitHeight
                Heading {
                    id: title
                    text: panel.hasTabs ? qsTr("Seasons and episodes") : qsTr("Episodes")
                    font.pixelSize: Theme.fontSizeLarge
                }
                Text {
                    objectName: "episodesWatched"
                    anchors.right: parent.right
                    anchors.verticalCenter: title.verticalCenter
                    visible: panel.episodes.length > 0
                    text: qsTr("%1 of %2 watched").arg(panel.watched).arg(panel.episodes.length)
                    color: Overlay.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                }
            }

            ListView {
                id: tabs
                objectName: "episodeSeasonTabs"
                width: parent.width
                height: Theme.controlHeight
                visible: panel.hasTabs
                orientation: ListView.Horizontal
                spacing: Theme.spacingLarge
                clip: true
                keyNavigationEnabled: true
                activeFocusOnTab: true
                boundsBehavior: Flickable.StopAtBounds
                model: panel.seasons
                delegate: T.AbstractButton {
                    id: tab
                    required property int modelData
                    readonly property bool selected: modelData === panel.season
                    height: tabs.height
                    implicitWidth: tabText.implicitWidth
                    focusPolicy: Qt.StrongFocus
                    hoverEnabled: true
                    Accessible.name: tabText.text
                    Accessible.role: Accessible.PageTab
                    Keys.onReturnPressed: tab.clicked()
                    Keys.onEnterPressed: tab.clicked()
                    onClicked: {
                        panel.chosenSeason = modelData
                        flick.contentY = 0
                        Qt.callLater(flick.scrollToCurrent)
                    }
                    contentItem: Text {
                        id: tabText
                        text: panel.seasonName(tab.modelData)
                        verticalAlignment: Text.AlignVCenter
                        color: tab.selected || tab.hovered ? Overlay.text : Overlay.textSecondary
                        font.pixelSize: Theme.fontSizeBody
                        font.weight: tab.selected ? Theme.fontWeightBold : Font.Normal
                    }
                    background: Item {
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 2
                            radius: 1
                            visible: tab.selected
                            color: Overlay.accent
                        }
                        FocusFrame { shown: tab.visualFocus }
                    }
                }
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Overlay.border
                visible: tabs.visible
            }

            Column {
                id: list
                width: parent.width
                spacing: Theme.spacingTiny

                Repeater {
                    id: rows
                    model: panel.shown
                    delegate: T.ItemDelegate {
                        id: row
                        required property var modelData
                        readonly property bool current: modelData.current === true
                        objectName: "episodeRow"
                        width: list.width
                        implicitHeight: Math.max(still.height, captions.implicitHeight) + 2 * Theme.spacingSmall
                        padding: Theme.spacingSmall
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        // The one that plays stays where it is.
                        enabled: !current
                        Accessible.name: modelData.name || ""
                        Keys.onReturnPressed: row.clicked()
                        Keys.onEnterPressed: row.clicked()
                        onClicked: panel.chosen(modelData.id)

                        background: Rectangle {
                            radius: Theme.radius
                            color: row.current || row.hovered || row.visualFocus ? Overlay.surfaceHover : "transparent"
                            FocusFrame { shown: row.visualFocus }
                        }

                        contentItem: Item {
                            Rectangle {
                                id: still
                                objectName: "episodeStill"
                                width: Theme.tvMode ? 200 : 128
                                height: Math.round(width * 9 / 16)
                                anchors.verticalCenter: parent.verticalCenter
                                radius: Theme.radius
                                color: Overlay.surfaceRaised
                                border.width: row.current ? 2 : 0
                                border.color: Overlay.accent
                                clip: true

                                Image {
                                    anchors.fill: parent
                                    anchors.margins: parent.border.width
                                    source: row.modelData.imageSource || ""
                                    sourceSize.width: still.width * 2
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                }
                                Rectangle {
                                    objectName: "episodeProgress"
                                    visible: (row.modelData.progress || 0) > 0
                                    anchors.left: parent.left
                                    anchors.bottom: parent.bottom
                                    width: parent.width * (row.modelData.progress || 0)
                                    height: 4
                                    color: Overlay.progress
                                }
                                Icon {
                                    objectName: "episodeWatched"
                                    visible: row.modelData.played === true
                                    anchors.top: parent.top
                                    anchors.right: parent.right
                                    anchors.margins: Theme.spacingSmall
                                    source: Icons.check
                                    color: Overlay.text
                                    size: 18
                                }
                            }

                            Column {
                                id: captions
                                anchors.left: still.right
                                anchors.leftMargin: Theme.spacing
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: Theme.spacingTiny

                                Text {
                                    width: parent.width
                                    visible: text.length > 0
                                    text: row.current ? (panel.code(row.modelData) + (panel.code(row.modelData) ? " · " : "") + qsTr("Now playing"))
                                                      : panel.code(row.modelData)
                                    color: row.current ? Overlay.accent : Overlay.textSecondary
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                                Text {
                                    width: parent.width
                                    text: row.modelData.name || ""
                                    color: Overlay.text
                                    font.pixelSize: Theme.fontSizeBody
                                    font.weight: Theme.fontWeightBold
                                    elide: Text.ElideRight
                                    wrapMode: Text.Wrap
                                    maximumLineCount: 2
                                }
                                Text {
                                    width: parent.width
                                    visible: text.length > 0
                                    text: panel.runTime(row.modelData.runTimeTicks)
                                    color: Overlay.textSecondary
                                    font.pixelSize: Theme.fontSizeSmall
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
