pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// One person (an actor, a director): the photo, name and biography, then
// the films and series they appear in as a grid, newest first.
RoutePage {
    id: page

    // Created once the route is known; the page owns both.
    property ItemDetails details: null
    property LibraryModel items: null

    Component.onCompleted: {
        details = Library.createDetails(routeArgument, page)
        items = Library.createPerson(routeArgument, page)
    }

    title: details && details.loaded ? details.name : qsTr("Person")

    // Back from a lost connection: what failed loads again.
    Connections {
        target: Library
        function onReconnected() {
            if (page.items && page.items.errorText.length > 0) page.items.reload()
            if (page.details && page.details.errorText.length > 0) page.details.reload()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingLarge

            Image {
                objectName: "personPhoto"
                Layout.preferredWidth: 120
                Layout.preferredHeight: 180
                Layout.alignment: Qt.AlignTop
                visible: status === Image.Ready
                source: page.details ? Library.image(page.details.posterUrl) : ""
                sourceSize.width: 120
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.spacing

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Theme.spacing

                    Heading {
                        objectName: "personName"
                        Layout.fillWidth: true
                        text: page.details && page.details.loaded ? page.details.name : ""
                    }
                    Text {
                        objectName: "personCount"
                        visible: page.items !== null && page.items.errorText.length === 0
                                 && !(page.items.loading && page.items.count === 0)
                        text: page.items ? qsTr("%n title(s)", "", page.items.count) : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    ActionButton {
                        objectName: "personFavorite"
                        visible: page.details && page.details.loaded
                        text: page.details && page.details.favorite ? qsTr("♥ Favorite") : qsTr("♡ Add to favorites")
                        onClicked: page.details.toggleFavorite()
                    }
                }

                Text {
                    objectName: "personOverview"
                    Layout.fillWidth: true
                    Layout.maximumHeight: 6 * font.pixelSize * 1.25
                    visible: text.length > 0
                    text: page.details ? page.details.overview : ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeBody
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                }
            }
        }

        StateMessage {
            objectName: "personError"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 200 : 0
            visible: description.length > 0
            kind: Library.online ? "error" : "offline"
            description: page.items && page.items.errorText.length > 0 ? page.items.errorText
                : page.details ? page.details.errorText : ""
            onRetry: {
                if (page.items) page.items.reload()
                if (page.details) page.details.reload()
            }
        }

        CardGrid {
            id: grid
            objectName: "personGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            model: page.items
            delegate: ItemCard {}

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.items && page.items.loading && grid.count === 0
            }
            StateMessage {
                objectName: "personEmpty"
                anchors.centerIn: parent
                visible: page.items && !page.items.loading && page.items.count === 0
                         && page.items.errorText.length === 0
                kind: "empty"
                title: qsTr("No films or series with this person.")
            }
        }
    }
}
