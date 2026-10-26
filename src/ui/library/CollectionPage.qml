pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// One collection: its backdrop, name and overview, then its items as a
// grid, by release date.
RoutePage {
    id: page

    // Created once the route is known; the page owns both.
    property ItemDetails details: null
    property LibraryModel items: null

    Component.onCompleted: {
        details = Library.createDetails(routeArgument, page)
        items = Library.createCollection(routeArgument, page)
    }

    title: details && details.loaded ? details.name : qsTr("Collection")

    Image {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: Math.min(parent.height * 0.5, width * 9 / 16)
        source: page.details ? Library.image(page.details.backdropUrl) : ""
        sourceSize.width: width
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        opacity: status === Image.Ready ? 0.3 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationSlow } }

        Rectangle {
            anchors.fill: parent
            gradient: Gradient {
                GradientStop { position: 0.3; color: "transparent" }
                GradientStop { position: 1.0; color: Theme.background }
            }
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
            spacing: Theme.spacing

            Heading {
                objectName: "collectionName"
                Layout.fillWidth: true
                text: page.details && page.details.loaded ? page.details.name : ""
            }
            Text {
                objectName: "collectionCount"
                text: page.items ? qsTr("%n item(s)", "", page.items.count) : ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall
            }
            ActionButton {
                objectName: "collectionFavorite"
                visible: page.details && page.details.loaded
                text: page.details && page.details.favorite ? qsTr("♥ Favorite") : qsTr("♡ Add to favorites")
                onClicked: page.details.toggleFavorite()
            }
        }

        Text {
            objectName: "collectionOverview"
            Layout.fillWidth: true
            Layout.maximumHeight: 4 * font.pixelSize * 1.25
            visible: text.length > 0
            text: page.details ? page.details.overview : ""
            color: Theme.text
            font.pixelSize: Theme.fontSizeBody
            lineHeight: 1.25
            wrapMode: Text.Wrap
            elide: Text.ElideRight
        }

        Text {
            objectName: "collectionError"
            Layout.fillWidth: true
            visible: text.length > 0
            text: page.items && page.items.errorText.length > 0 ? page.items.errorText
                : page.details ? page.details.errorText : ""
            color: Theme.error
            font.pixelSize: Theme.fontSizeBody
            wrapMode: Text.Wrap
        }

        CardGrid {
            id: grid
            objectName: "collectionGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            model: page.items
            delegate: ItemCard {}

            LoadingIndicator {
                anchors.centerIn: parent
                visible: page.items && page.items.loading && grid.count === 0
            }
            Text {
                anchors.centerIn: parent
                visible: page.items && !page.items.loading && page.items.count === 0
                         && page.items.errorText.length === 0
                text: qsTr("This collection is empty.")
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeBody
            }
        }
    }
}
