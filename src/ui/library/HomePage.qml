pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic as Basic
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// Home: the user's libraries, then Continue watching, Next up and the
// latest items of each library. Up and Down move between rows, Left and
// Right along a row. Coming back to the page refreshes the rows in place.
RoutePage {
    id: page

    readonly property HomeModel home: Library.home

    title: qsTr("Home")

    Component.onCompleted: if (home) home.reload()
    Basic.StackView.onActivated: if (home && home.count > 0) home.reload()

    ListView {
        id: rows
        objectName: "homeRows"
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingHuge
        anchors.rightMargin: Theme.spacingHuge
        focus: true
        clip: true
        spacing: Theme.spacingHuge
        keyNavigationEnabled: true
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: Theme.durationNormal
        highlightRangeMode: ListView.ApplyRange
        preferredHighlightBegin: Theme.spacingHuge
        preferredHighlightEnd: height - Theme.spacingHuge
        // Rows are tall; keep one more on each side ready.
        cacheBuffer: 600
        model: page.home

        header: Column {
            width: rows.width
            topPadding: Theme.spacingLarge
            bottomPadding: Theme.spacingHuge
            spacing: Theme.spacingLarge

            Text {
                objectName: "homeError"
                width: parent.width
                visible: text.length > 0
                text: page.home ? page.home.errorText : ""
                color: Theme.error
                font.pixelSize: Theme.fontSizeBody
                wrapMode: Text.Wrap
            }

            CardRow {
                id: libraries
                objectName: "librariesRow"
                width: parent.width
                visible: count > 0
                title: qsTr("My media")
                model: page.home ? page.home.libraries : null
                cardHeight: Math.round(Theme.thumbWidth / Theme.thumbAspect) + 52
                delegate: ItemCard { shape: "thumb" }
                KeyNavigation.down: rows
            }

            LoadingIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.home && page.home.loading && page.home.count === 0
            }
        }

        delegate: CardRow {
            id: row
            required title
            required property string kind
            required property var items
            required property string cardShape
            required property int index

            objectName: "homeRow_" + kind
            width: rows.width
            focus: ListView.isCurrentItem
            model: items
            cardHeight: cardShape === "thumb" ? Math.round(Theme.thumbWidth / Theme.thumbAspect) + 52
                                              : Math.round(Theme.posterWidth / Theme.posterAspect) + 52
            delegate: ItemCard { shape: row.cardShape }
        }

        footer: Item { width: 1; height: Theme.spacingHuge }
    }

    Text {
        anchors.centerIn: parent
        visible: page.home && !page.home.loading && page.home.count === 0
                 && page.home.libraries.count === 0 && page.home.errorText.length === 0
        text: qsTr("Nothing here yet. Add media to a library on the server.")
        color: Theme.textSecondary
        font.pixelSize: Theme.fontSizeBody
    }
}
