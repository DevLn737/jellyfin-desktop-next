pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The settings screen, generated from the registered sections: on the left
// a search field and the list of sections, on the right the settings, one
// card per section with a row per setting and an editor for its type.
// Nothing here knows individual settings; modules add theirs by
// registering a section. The field (Ctrl+F) leaves the settings whose
// words match, in either language, with the match marked; Esc in it clears
// it, Down goes to the list. A section in the list scrolls to its card.
RoutePage {
    id: page

    title: qsTr("Settings")

    // Narrow windows drop the section list; the search field stays on top.
    readonly property bool wide: width >= 900
    readonly property int sideWidth: wide ? Math.min(300, Math.round(width * 0.24)) : 0
    // The section whose card is at the top of the list.
    property string currentSection: ""

    function iconFor(id) {
        switch (id) {
        case "playback": return Icons.play
        case "subtitles": return Icons.subtitles
        case "audio": return Icons.audio
        case "interface": return Icons.tune
        case "appearance": return Icons.palette
        case "window": return Icons.window
        case "library": return Icons.library
        case "music": return Icons.music
        case "input": return Icons.gamepad
        case "syncplay": return Icons.group
        case "downloads": return Icons.download
        case "integrations": case "remote": return Icons.link
        case "account": case "diagnostics": return Icons.info
        default: return Icons.settings
        }
    }

    function showSection(row) {
        list.positionViewAtIndex(row, ListView.Beginning)
        // The card's header is above its first row.
        list.contentY = Math.max(list.originY - list.topMargin, list.contentY - Theme.spacingHuge - 28)
        list.currentIndex = row
    }

    function updateCurrent() {
        // At the end, the section chosen last stays (the last ones cannot
        // reach the top).
        if (list.atYEnd && page.currentSection.length > 0 && list.contentY > list.originY) return
        const item = list.itemAt(list.width / 2, list.contentY + list.topMargin + 40)
        const sections = search.sections
        if (item && item.sectionId) page.currentSection = item.sectionId
        else if (sections.length > 0 && page.currentSection.length === 0) page.currentSection = sections[0].id
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: searchField.forceActiveFocus()
    }

    SettingsSearch {
        id: search
        settings: AppSettings.model
        text: searchField.text
        onCountChanged: Qt.callLater(page.updateCurrent)
    }

    // The left side: search and sections.
    Item {
        id: side
        x: Theme.pageMargin
        y: Theme.spacingLarge
        width: page.wide ? page.sideWidth : page.width - 2 * Theme.pageMargin
        height: page.wide ? page.height - y : searchField.height

        InputField {
            id: searchField
            objectName: "settingsSearch"
            width: parent.width
            field.leftPadding: Theme.spacing + 30
            placeholderText: qsTr("Search settings")
            Accessible.name: qsTr("Search settings")
            Icon {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing
                anchors.verticalCenter: parent.verticalCenter
                source: Icons.search
                size: 18
                color: Theme.textSecondary
            }
            Keys.onEscapePressed: (event) => {
                event.accepted = text.length > 0
                text = ""
            }
            Keys.onDownPressed: page.wide && sectionList.count > 0 ? sectionList.focusCurrent() : list.forceActiveFocus()
            onAccepted: list.forceActiveFocus()
        }

        Flickable {
            id: sectionList
            objectName: "settingsSections"
            readonly property int count: sectionRepeater.count
            visible: page.wide
            anchors.top: searchField.bottom
            anchors.topMargin: Theme.spacingLarge
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingLarge
            width: parent.width
            clip: true
            contentHeight: sectionColumn.implicitHeight
            boundsBehavior: Flickable.StopAtBounds

            function focusAt(index) {
                const item = sectionRepeater.itemAt(Math.max(0, Math.min(index, sectionRepeater.count - 1)))
                if (item) item.forceActiveFocus(Qt.TabFocusReason)
            }
            function focusCurrent() {
                let index = 0
                for (let i = 0; i < sectionRepeater.count; ++i)
                    if (sectionRepeater.itemAt(i).current) index = i
                focusAt(index)
            }

            Column {
                id: sectionColumn
                width: parent.width
                spacing: 2
                Repeater {
                    id: sectionRepeater
                    model: search.sections
                    delegate: T.ItemDelegate {
                        id: sectionTab
                        required property var modelData
                        required property int index
                        readonly property bool current: page.currentSection === modelData.id
                        objectName: "settingsSection_" + modelData.id
                        width: sectionColumn.width
                        implicitHeight: Theme.controlHeight + 6
                        leftPadding: Theme.spacing
                        rightPadding: Theme.spacing
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        text: modelData.title
                        Keys.onUpPressed: index > 0 ? sectionList.focusAt(index - 1) : searchField.forceActiveFocus()
                        Keys.onDownPressed: sectionList.focusAt(index + 1)
                        Keys.onRightPressed: list.forceActiveFocus()
                        Accessible.name: modelData.title
                        Keys.onReturnPressed: sectionTab.clicked()
                        Keys.onEnterPressed: sectionTab.clicked()
                        onClicked: {
                            page.showSection(modelData.row)
                            page.currentSection = modelData.id
                        }
                        contentItem: Row {
                            spacing: Theme.spacing
                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                source: page.iconFor(sectionTab.modelData.id)
                                size: 18
                                color: sectionTab.current ? Theme.text : Theme.textSecondary
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                width: sectionTab.availableWidth - 18 - Theme.spacing
                                text: sectionTab.text
                                elide: Text.ElideRight
                                color: sectionTab.current || sectionTab.hovered ? Theme.text : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: sectionTab.current ? Font.ExtraBold : Font.Bold
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radius + 2
                            color: sectionTab.current ? Qt.alpha(Theme.text, 0.09)
                                 : sectionTab.hovered ? Qt.alpha(Theme.text, 0.04) : "transparent"
                            FocusFrame { shown: sectionTab.visualFocus }
                        }
                    }
                }
            }
        }
    }

    // The right side: a card per section.
    ListView {
        SmoothWheel {}
        id: list
        objectName: "settingsList"
        x: page.wide ? side.x + side.width + Theme.spacingHuge : 0
        y: page.wide ? 0 : side.y + side.height + Theme.spacingSmall
        width: page.wide ? Math.min(page.width - x - Theme.pageMargin, 1100) : page.width
        height: page.height - y
        leftMargin: page.wide ? 0 : Theme.pageMargin
        rightMargin: page.wide ? 0 : Theme.pageMargin
        topMargin: Theme.spacingLarge
        bottomMargin: Theme.spacingHuge
        clip: true
        focus: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        spacing: 0
        model: search
        Keys.onLeftPressed: (event) => {
            event.accepted = page.wide
            if (page.wide) sectionList.focusCurrent()
        }
        onContentYChanged: page.updateCurrent()
        onCurrentIndexChanged: if (currentItem && currentItem.activeFocus) page.updateCurrent()

        section.property: "sectionTitle"
        section.delegate: Item {
            required property string section
            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            implicitHeight: title.implicitHeight + Theme.spacingLarge + Theme.spacing
            Text {
                id: title
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.spacing
                text: parent.section
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge + 4
                font.weight: Font.ExtraBold
            }
        }

        delegate: Item {
            id: cell
            required property int index
            required property string sectionId
            required property string key
            required property string type
            required property string label
            required property string description
            required property var value
            required property var choices
            required property real minimum
            required property real maximum
            required property real step
            required property string unit
            required property bool requiresRestart
            required property bool isDefault
            required property string confirmation
            readonly property bool first: ListView.previousSection !== ListView.section
            readonly property bool last: ListView.nextSection !== ListView.section

            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            implicitHeight: settingRow.implicitHeight + (first ? Theme.spacingSmall : 0) + (last ? Theme.spacingSmall : 0)
            focus: ListView.isCurrentItem
            Accessible.ignored: true

            // The section's card, drawn a piece per row.
            Rectangle {
                anchors.fill: parent
                color: Theme.surface
                topLeftRadius: cell.first ? Theme.radiusLarge : 0
                topRightRadius: cell.first ? Theme.radiusLarge : 0
                bottomLeftRadius: cell.last ? Theme.radiusLarge : 0
                bottomRightRadius: cell.last ? Theme.radiusLarge : 0
            }
            Rectangle {
                visible: !cell.first
                x: Theme.spacingLarge
                width: parent.width - 2 * Theme.spacingLarge
                height: 1
                color: Theme.border
                opacity: 0.6
            }

            SettingRow {
                id: settingRow
                x: Theme.spacingSmall
                y: cell.first ? Theme.spacingSmall : 0
                width: parent.width - Theme.spacingSmall - Theme.spacingLarge
                focus: true
                highlight: search.text
                key: cell.key
                type: cell.type
                label: cell.label
                description: cell.description
                value: cell.value
                choices: cell.choices
                minimum: cell.minimum
                maximum: cell.maximum
                step: cell.step
                unit: cell.unit
                requiresRestart: cell.requiresRestart
                confirmation: cell.confirmation
            }
        }
    }

    StateMessage {
        objectName: "settingsNothingFound"
        anchors.centerIn: list
        visible: searchField.text.trim().length > 0 && search.count === 0
        kind: "empty"
        title: qsTr("No settings match “%1”.").arg(searchField.text.trim())
    }
}
