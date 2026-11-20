pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Window
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Player

// The player's settings: a list of rows with their values (quality, speed,
// the subtitles' look and delay, the audio delay), the automatic skip of
// intros and credits, and the statistics. A row with a value opens its
// choices in place, under a header that goes back. Only what the player
// can change now is listed.
Rectangle {
    id: menu

    // Alone always; in a group only when the server can change its speed
    // (then it asks the group, so everyone follows).
    property bool speedAvailable: true
    // The statistics overlay shows.
    property bool statsShown: false
    // "" (the list), "quality", "speed", "subtitles" or "audio".
    property string section: ""

    signal statsToggled()

    readonly property var speeds: [0.5, 0.75, 1, 1.25, 1.5, 1.75, 2]
    // Bits per second; 0 is the original.
    readonly property var qualities: [0, 20000000, 10000000, 6000000, 3000000, 1500000]
    // mpv's sub-font-size, as in the settings; -1 the default.
    readonly property var sizes: [
        { value: -1, label: qsTr("Default") },
        { value: 24, label: qsTr("Small") },
        { value: 32, label: qsTr("Normal") },
        { value: 42, label: qsTr("Large") },
        { value: 60, label: qsTr("Huge") },
    ]

    function qualityText(bits) {
        return bits === 0 ? qsTr("Original") : qsTr("%1 Mbit/s").arg(bits / 1000000)
    }
    function sizeText(size) {
        for (const entry of sizes)
            if (entry.value === size) return entry.label
        return size > 0 ? size.toString() : qsTr("Default")
    }
    function delayText(seconds) {
        return (seconds > 0 ? "+" : "") + Number(seconds).toLocaleString(Qt.locale(), "f", 1) + " " + qsTr("s")
    }
    function pageTitle() {
        return section === "quality" ? qsTr("Quality")
            : section === "speed" ? qsTr("Speed")
            : section === "subtitles" ? qsTr("Subtitles: look and delay")
            : section === "audio" ? qsTr("Audio delay")
            : qsTr("Player settings")
    }

    // Keys and the TV mode: the first row of what shows.
    function focusFirst() {
        const first = content.nextItemInFocusChain(true)
        for (let item = first; item; item = item.parent) {
            if (item === content) {
                first.forceActiveFocus(Qt.TabFocusReason)
                return
            }
        }
    }
    // A page opened by keys keeps the keys on its rows.
    onSectionChanged: if (Theme.tvMode || Window.activeFocusItem && holds(Window.activeFocusItem)) Qt.callLater(focusFirst)

    function holds(item) {
        for (let at = item; at; at = at.parent)
            if (at === menu) return true
        return false
    }

    objectName: "tuneMenu"
    width: 420
    height: Math.min(content.implicitHeight + 2 * Theme.spacing, 560)
    radius: Theme.radiusLarge
    color: Overlay.surface
    border.color: Overlay.border

    Keys.onEscapePressed: (event) => {
        if (menu.section.length === 0) { event.accepted = false; return }
        menu.section = ""
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacing
        contentHeight: content.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: content
            width: parent.width
            spacing: 2

            // The header: the gear, or a way back from a page.
            Row {
                height: Theme.controlHeight
                spacing: Theme.spacingSmall
                IconButton {
                    objectName: "tuneBack"
                    anchors.verticalCenter: parent.verticalCenter
                    visible: menu.section.length > 0
                    focusPolicy: Qt.TabFocus
                    iconSource: Icons.back
                    toolTipText: qsTr("Back")
                    onClicked: menu.section = ""
                }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: menu.section.length === 0
                    source: Icons.settings
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: menu.pageTitle()
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeLarge - 2
                    font.weight: Font.ExtraBold
                }
            }
            Rectangle {
                width: parent.width
                height: 1
                color: Overlay.border
            }

            // The list.
            Column {
                width: parent.width
                visible: menu.section.length === 0
                spacing: 2
                topPadding: Theme.spacingTiny

                SettingRow {
                    objectName: "tuneQuality"
                    text: PlayerSession.playMethod === "Transcode" ? qsTr("Quality (converted by the server)") : qsTr("Quality")
                    value: menu.qualityText(PlayerSession.maxBitrate)
                    onClicked: menu.section = "quality"
                }
                SettingRow {
                    objectName: "tuneSpeed"
                    visible: menu.speedAvailable
                    text: PlayerSession.inGroup ? qsTr("Speed (for everyone)") : qsTr("Speed")
                    value: PlayerSession.baseRate + "×"
                    onClicked: menu.section = "speed"
                }
                SettingRow {
                    objectName: "tuneSubtitles"
                    text: qsTr("Subtitles: look and delay")
                    value: menu.sizeText(PlayerSession.subtitleSize)
                    onClicked: menu.section = "subtitles"
                }
                SettingRow {
                    objectName: "tuneAudio"
                    text: qsTr("Audio delay")
                    value: menu.delayText(PlayerSession.audioDelay)
                    onClicked: menu.section = "audio"
                }
                SettingRow {
                    objectName: "tuneAutoSkip"
                    text: qsTr("Skip intros and credits")
                    toggle: true
                    checked: PlayerSession.autoSkip
                    onClicked: PlayerSession.autoSkip = !PlayerSession.autoSkip
                }
                SettingRow {
                    objectName: "tuneStats"
                    text: menu.statsShown ? qsTr("Hide stats for nerds") : qsTr("Stats for nerds")
                    value: "I"
                    chevron: false
                    onClicked: menu.statsToggled()
                }
            }

            // Quality.
            Column {
                width: parent.width
                visible: menu.section === "quality"
                spacing: 2
                topPadding: Theme.spacingTiny
                Repeater {
                    model: menu.section === "quality" ? menu.qualities : []
                    ChoiceRow {
                        required property real modelData
                        objectName: "quality" + modelData
                        text: menu.qualityText(modelData)
                        selected: PlayerSession.maxBitrate === modelData
                        onClicked: PlayerSession.maxBitrate = modelData
                    }
                }
            }

            // Speed.
            Column {
                width: parent.width
                visible: menu.section === "speed"
                spacing: 2
                topPadding: Theme.spacingTiny
                Repeater {
                    model: menu.section === "speed" ? menu.speeds : []
                    ChoiceRow {
                        required property real modelData
                        objectName: "speed" + modelData
                        text: modelData + "×"
                        selected: Math.abs(PlayerSession.baseRate - modelData) < 0.001
                        onClicked: PlayerSession.setRate(modelData)
                    }
                }
            }

            // The subtitles: size, position and delay, live on the video.
            Column {
                width: parent.width
                visible: menu.section === "subtitles"
                spacing: Theme.spacingSmall
                topPadding: Theme.spacingSmall

                Caption { text: qsTr("Size") }
                Flow {
                    width: parent.width
                    spacing: Theme.spacingSmall
                    Repeater {
                        model: menu.sizes
                        ActionButton {
                            required property var modelData
                            objectName: "subtitleSize" + modelData.value
                            text: modelData.label
                            primary: PlayerSession.subtitleSize === modelData.value
                            onClicked: PlayerSession.subtitleSize = modelData.value
                        }
                    }
                }
                Caption { text: qsTr("Position") }
                Flow {
                    width: parent.width
                    spacing: Theme.spacingSmall
                    Repeater {
                        model: [
                            { value: "", label: qsTr("Bottom") },
                            { value: "center,top", label: qsTr("Top") },
                        ]
                        ActionButton {
                            required property var modelData
                            text: modelData.label
                            primary: PlayerSession.subtitlePlacement === modelData.value
                            onClicked: PlayerSession.subtitlePlacement = modelData.value
                        }
                    }
                }
                Caption { text: qsTr("Delay") }
                DelayRow {
                    objectName: "subtitleDelayRow"
                    value: PlayerSession.subtitleDelay
                    onAdjusted: (value) => PlayerSession.subtitleDelay = value
                }
                Text {
                    width: parent.width
                    text: qsTr("Size and position apply to plain subtitles (SRT, VTT); more in Settings.")
                    color: Overlay.textSecondary
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.Wrap
                }
            }

            // The audio delay.
            Column {
                width: parent.width
                visible: menu.section === "audio"
                spacing: Theme.spacingSmall
                topPadding: Theme.spacingSmall
                DelayRow {
                    objectName: "audioDelayRow"
                    value: PlayerSession.audioDelay
                    onAdjusted: (value) => PlayerSession.audioDelay = value
                }
            }
        }
    }

    // A row of the list: its name, its value and a chevron, or a switch.
    component SettingRow: T.ItemDelegate {
        id: row
        property string value
        property bool toggle: false
        property bool chevron: !toggle
        width: content.width
        implicitHeight: Theme.controlHeight + Theme.spacingTiny
        leftPadding: Theme.spacing
        rightPadding: Theme.spacing
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        checkable: false
        Accessible.name: text
        Keys.onReturnPressed: row.clicked()
        Keys.onEnterPressed: row.clicked()

        contentItem: Item {
            Text {
                anchors.left: parent.left
                anchors.right: trailing.left
                anchors.rightMargin: Theme.spacing
                anchors.verticalCenter: parent.verticalCenter
                text: row.text
                color: Overlay.text
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
            }
            Row {
                id: trailing
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSmall
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: !row.toggle && row.value.length > 0
                    text: row.value
                    color: Overlay.textSecondary
                    font.pixelSize: Theme.fontSizeBody
                }
                Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.chevron
                    source: Icons.right
                    size: 16
                    color: Overlay.textSecondary
                }
                ToggleSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: row.toggle
                    checked: row.checked
                    focusPolicy: Qt.NoFocus
                    onToggled: row.clicked()
                }
            }
        }
        background: Rectangle {
            radius: Theme.radius
            color: row.hovered || row.visualFocus ? Overlay.surfaceHover : "transparent"
            FocusFrame { shown: row.visualFocus }
        }
    }

    // One choice of a page, with a check on the one in use.
    component ChoiceRow: ListItem {
        property bool selected: false
        width: content.width
        iconSource: selected ? Icons.check : ""
        reserveIcon: true
        highlighted: selected
    }

    component Caption: Text {
        color: Overlay.textSecondary
        font.pixelSize: Theme.fontSizeSmall
        font.weight: Theme.fontWeightBold
    }

    component DelayRow: Row {
        id: delay
        property real value
        signal adjusted(real value)
        spacing: Theme.spacingSmall
        ActionButton { text: "−0.1"; onClicked: delay.adjusted(delay.value - 0.1) }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 76
            horizontalAlignment: Text.AlignHCenter
            text: menu.delayText(delay.value)
            color: Overlay.text
            font.pixelSize: Theme.fontSizeBody
            font.weight: Theme.fontWeightBold
        }
        ActionButton { text: "+0.1"; onClicked: delay.adjusted(delay.value + 0.1) }
        ActionButton { text: qsTr("Reset"); enabled: delay.value !== 0; onClicked: delay.adjusted(0) }
    }
}
