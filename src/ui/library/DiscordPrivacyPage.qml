pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Settings
import Jfd.Ui.Library

// "What to show in Discord": a switch for movies, series, music and TV, and
// one for each library; what is switched off never reaches the Discord card.
// The kinds are integrations/discord{Movies,Series,Music,LiveTv}; the
// libraries switched off are integrations/discordHiddenLibraries (ids
// separated by commas).
RoutePage {
    id: page

    readonly property string librariesKey: "integrations/discordHiddenLibraries"

    title: qsTr("What to show in Discord")

    function hiddenLibraries() {
        const value = AppSettings.revision >= 0 ? String(AppSettings.value(page.librariesKey) || "") : ""
        return value.length > 0 ? value.split(",") : []
    }
    function setLibraryShown(id, shown) {
        let ids = hiddenLibraries().filter(other => other !== id)
        if (!shown) ids.push(id)
        AppSettings.setValue(page.librariesKey, ids.join(","))
    }

    component SwitchRow: FocusScope {
        id: row
        property string text
        property bool checked
        signal toggled(bool checked)
        width: parent ? parent.width : 0
        height: Theme.controlHeight + Theme.spacingSmall
        activeFocusOnTab: true
        Accessible.name: text

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: row.activeFocus ? Theme.surfaceHover : Theme.surface
        }
        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.spacing
            anchors.rightMargin: Theme.spacing
            spacing: Theme.spacing
            ToggleSwitch {
                id: toggle
                objectName: row.objectName + "_switch"
                anchors.verticalCenter: parent.verticalCenter
                focus: true
                checked: row.checked
                Accessible.name: row.text
                onToggled: row.toggled(checked)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width - toggle.width - parent.spacing
                text: row.text
                color: row.checked ? Theme.text : Theme.textDisabled
                font.pixelSize: Theme.fontSizeBody
                elide: Text.ElideRight
            }
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.bottomMargin: footer.height
        contentHeight: content.implicitHeight + 2 * Theme.spacingLarge
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        SmoothWheel {}

        Column {
            id: content
            x: Theme.pageMargin
            y: Theme.spacingLarge
            width: flick.width - 2 * Theme.pageMargin
            spacing: Theme.spacingTiny

            Heading { text: page.title }
            Text {
                width: parent.width
                topPadding: Theme.spacingSmall
                bottomPadding: Theme.spacing
                text: qsTr("What is switched off is never shown on your Discord status.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: [
                    { key: "integrations/discordMovies", label: qsTr("Movies") },
                    { key: "integrations/discordSeries", label: qsTr("Series") },
                    { key: "integrations/discordMusic", label: qsTr("Music") },
                    { key: "integrations/discordLiveTv", label: qsTr("TV channels") },
                ]
                SwitchRow {
                    required property var modelData
                    objectName: "discordKind_" + modelData.key.split("/")[1]
                    text: modelData.label
                    checked: AppSettings.revision >= 0 && AppSettings.value(modelData.key) !== false
                    onToggled: checked => AppSettings.setValue(modelData.key, checked)
                }
            }

            Text {
                width: parent.width
                topPadding: Theme.spacingLarge
                bottomPadding: Theme.spacingSmall
                text: qsTr("Libraries")
                color: Theme.text
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.Bold
            }
            Repeater {
                id: libraries
                model: Library.home ? Library.home.libraries : null
                SwitchRow {
                    required property var model
                    readonly property string libraryId: model.itemId || ""
                    objectName: "discordLibrary_" + libraryId
                    text: model.name || ""
                    checked: page.hiddenLibraries().indexOf(libraryId) < 0
                    onToggled: checked => page.setLibraryShown(libraryId, checked)
                }
            }
            Text {
                visible: libraries.count === 0
                width: parent.width
                text: qsTr("The libraries appear here once you are signed in.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.WordWrap
            }
        }
    }

    Row {
        id: footer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pageMargin
        anchors.bottomMargin: Theme.spacingLarge
        height: Theme.controlHeight + Theme.spacingLarge
        ActionButton {
            objectName: "doneDiscordPrivacy"
            anchors.verticalCenter: parent.verticalCenter
            primary: true
            text: qsTr("Done")
            onClicked: Navigator.back()
        }
    }
}
