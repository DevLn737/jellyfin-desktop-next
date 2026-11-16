import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The settings screen, generated from the registered sections: one row per
// setting with an editor for its type. Nothing here knows individual
// settings; modules add theirs by registering a section. The field at the
// top (Ctrl+F) leaves the settings whose words match, in either language,
// with the match marked; Esc in it clears it, Down goes to the list.
RoutePage {
    id: page

    title: qsTr("Settings")

    Heading {
        id: heading
        x: Theme.spacingHuge
        y: Theme.spacingLarge
        text: page.title
    }

    InputField {
        id: searchField
        objectName: "settingsSearch"
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingHuge
        anchors.verticalCenter: heading.verticalCenter
        width: Math.min(360, parent.width - heading.implicitWidth - 3 * Theme.spacingHuge)
        placeholderText: qsTr("Search settings")
        Accessible.name: qsTr("Search settings")
        Keys.onEscapePressed: (event) => {
            event.accepted = text.length > 0
            text = ""
        }
        Keys.onDownPressed: list.forceActiveFocus()
        onAccepted: list.forceActiveFocus()
    }

    Shortcut {
        sequences: [StandardKey.Find]
        onActivated: searchField.forceActiveFocus()
    }

    SettingsSearch {
        id: search
        settings: AppSettings.model
        text: searchField.text
    }

    ListView {
        SmoothWheel {}
        id: list
        objectName: "settingsList"
        anchors.top: heading.bottom
        anchors.topMargin: Theme.spacingLarge
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        leftMargin: Theme.spacingHuge
        rightMargin: Theme.spacingHuge
        bottomMargin: Theme.spacingHuge
        clip: true
        focus: true
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        spacing: Theme.spacingTiny
        model: search

        section.property: "sectionTitle"
        section.delegate: SectionHeader {
            required property string section
            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            topPadding: Theme.spacingLarge
            bottomPadding: Theme.spacingSmall
            text: section
        }

        delegate: SettingRow {
            width: ListView.view.width - ListView.view.leftMargin - ListView.view.rightMargin
            focus: ListView.isCurrentItem
            highlight: search.text
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
