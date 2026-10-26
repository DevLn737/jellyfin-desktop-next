pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Layouts
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell

// A genre: route "genre/<name>". Its albums.
RoutePage {
    id: page

    readonly property var list: {
        const model = Music.createList("albums", page)
        model.genre = routeArgument
        return model
    }

    title: routeArgument

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

        CardGrid {
            id: grid
            objectName: "musicGrid"
            Layout.fillWidth: true
            Layout.fillHeight: true
            focus: true
            model: page.list
            cardHeight: Math.round(Theme.squareWidth / Theme.squareAspect) + 52
            delegate: MusicCard {
                listModel: page.list
                guard: guard
            }
        }
    }
}
