pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// A genre: route "genre/<name>", the name percent-encoded (a name like
// "Hip-Hop/Rap" has a slash, which a route argument cannot). Its albums.
RoutePage {
    id: page

    readonly property string genreName: {
        try {
            return decodeURIComponent(routeArgument)
        } catch (error) {
            return routeArgument
        }
    }

    readonly property var list: {
        const model = Music.createList("albums", page)
        model.genre = page.genreName
        return model
    }

    title: genreName

    // Back from a lost connection: what failed loads again.
    Connections {
        target: Library
        function onReconnected() { if (page.list.errorText.length > 0) page.list.reload() }
    }

    PlayGuard { id: guard }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingHuge
        anchors.rightMargin: Theme.spacingHuge
        anchors.topMargin: Theme.spacingLarge
        spacing: Theme.spacingLarge

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacing
            Heading {
                Layout.fillWidth: true
                text: page.title
            }
        }

        StateMessage {
            objectName: "musicError"
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? implicitHeight : 0
            visible: description.length > 0
            kind: Library.online ? "error" : "offline"
            description: page.list.errorText
            onRetry: page.list.reload()
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            CardGrid {
                id: grid
                objectName: "musicGrid"
                anchors.fill: parent
                focus: true
                model: page.list
                cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + Theme.cardCaptionHeight
                delegate: MusicCard {
                    listModel: page.list
                    guard: guard
                }
            }
            LoadingIndicator {
                objectName: "musicLoading"
                anchors.centerIn: parent
                visible: page.list.loading && page.list.count === 0
            }
            StateMessage {
                objectName: "musicEmpty"
                anchors.centerIn: parent
                width: Math.min(parent.width, implicitWidth)
                visible: page.list.loaded && page.list.count === 0 && page.list.errorText.length === 0
                kind: "empty"
                title: qsTr("No albums in this genre.")
            }
        }
    }
}
