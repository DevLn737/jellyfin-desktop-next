pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Search as you type: the query goes out once typing pauses, Enter sends
// it at once, and only the latest text's results are shown. Down moves
// from the field into the results.
RoutePage {
    id: page

    readonly property SearchModel search: Library.search

    title: qsTr("Search")

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        InputField {
            id: field
            objectName: "searchField"
            Layout.fillWidth: true
            Layout.maximumWidth: 640
            focus: true
            placeholderText: qsTr("Films, series, episodes, people")
            text: page.search ? page.search.text : ""
            inputMethodHints: Qt.ImhNoPredictiveText
            onTextChanged: if (page.search && page.search.text !== text) page.search.text = text
            onAccepted: if (page.search) page.search.submit()
            KeyNavigation.down: grid
        }

        Text {
            objectName: "searchError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.search ? page.search.errorText : ""
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        CardGrid {
            id: grid
            objectName: "searchResults"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: page.search ? page.search.results : null
            delegate: ItemCard {}

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.search && page.search.loading && grid.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: page.search && !page.search.loading && grid.count === 0
                         && page.search.text.trim().length > 0 && page.search.errorText.length === 0
                text: qsTr("Nothing found.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }
}
