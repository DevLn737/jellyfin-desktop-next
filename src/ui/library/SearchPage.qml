pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Search as you type: a big field with the query going out once typing
// pauses (Enter sends it at once, only the latest text's results show),
// then tabs by kind with their counts (all, films, series, episodes,
// people, music) and the results. Down moves from the field into the
// results.
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
            Layout.maximumWidth: 880
            focus: true
            field.implicitHeight: Theme.controlHeight + 20
            field.leftPadding: Theme.spacing + 36
            field.font.pixelSize: Theme.fontSizeLarge
            Icon {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing + 4
                anchors.verticalCenter: parent.verticalCenter
                source: Icons.search
                color: Theme.textSecondary
            }
            // Exactly what SearchModel asks the server for.
            placeholderText: qsTr("Films, series, episodes, music, people")
            text: page.search ? page.search.text : ""
            inputMethodHints: Qt.ImhNoPredictiveText
            onTextChanged: if (page.search && page.search.text !== text) page.search.text = text
            onAccepted: if (page.search) page.search.submit()
            KeyNavigation.down: grid
        }

        // The kinds, with how many of each the answer has.
        Rectangle {
            id: kindsFrame
            objectName: "searchKinds"
            readonly property int padding: 5
            readonly property var counts: page.search ? page.search.counts : ({})
            visible: (counts.all || 0) > 0
            Layout.preferredWidth: Math.min(kindRow.implicitWidth + 2 * padding, page.width - 2 * Theme.pageMargin)
            Layout.preferredHeight: Theme.controlHeight + 4 + 2 * padding
            radius: Theme.radiusLarge + 2
            color: Qt.alpha(Theme.text, 0.04)
            border.width: 1
            border.color: Theme.glassBorder
            clip: true

            Row {
                id: kindRow
                x: kindsFrame.padding
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.controlHeight + 4
                spacing: 2
                Repeater {
                    model: [
                        { kind: "all", label: qsTr("All") },
                        { kind: "movies", label: qsTr("Films") },
                        { kind: "series", label: qsTr("Series") },
                        { kind: "episodes", label: qsTr("Episodes") },
                        { kind: "people", label: qsTr("People") },
                        { kind: "music", label: qsTr("Music") },
                    ]
                    delegate: T.ItemDelegate {
                        id: kindTab
                        required property var modelData
                        readonly property int count: kindsFrame.counts[modelData.kind] || 0
                        readonly property bool current: page.search !== null && page.search.kind === modelData.kind
                        objectName: "searchKind_" + modelData.kind
                        visible: count > 0 || modelData.kind === "all"
                        implicitWidth: implicitContentWidth + leftPadding + rightPadding
                        height: kindRow.height
                        leftPadding: 14
                        rightPadding: 14
                        focusPolicy: Qt.StrongFocus
                        hoverEnabled: true
                        Accessible.name: modelData.label
                        Accessible.role: Accessible.PageTab
                        Keys.onReturnPressed: kindTab.clicked()
                        Keys.onEnterPressed: kindTab.clicked()
                        onClicked: if (page.search) page.search.kind = modelData.kind
                        contentItem: Row {
                            spacing: Theme.spacingSmall
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: kindTab.modelData.label
                                color: kindTab.current || kindTab.hovered ? Theme.text : Theme.textSecondary
                                font.pixelSize: Theme.fontSizeBody
                                font.weight: Font.Bold
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: String(kindTab.count)
                                color: Theme.textDisabled
                                font.pixelSize: Theme.fontSizeSmall
                                font.weight: Font.Bold
                            }
                        }
                        background: Rectangle {
                            radius: Theme.radius + 3
                            color: kindTab.current ? Qt.alpha(Theme.text, 0.12)
                                 : kindTab.hovered ? Qt.alpha(Theme.text, 0.05) : "transparent"
                            FocusFrame { shown: kindTab.visualFocus; inset: 0 }
                        }
                    }
                }
            }
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
