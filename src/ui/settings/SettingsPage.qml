import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// The settings screen, generated from the registered sections: one row per
// setting with an editor for its type. Nothing here knows individual
// settings; modules add theirs by registering a section.
RoutePage {
    id: page

    title: qsTr("Settings")

    Heading {
        id: heading
        x: Theme.spacingHuge
        y: Theme.spacingLarge
        text: page.title
    }

    ListView {
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
        model: AppSettings.model

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
        }
    }
}
