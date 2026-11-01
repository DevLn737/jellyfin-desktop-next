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
        SmoothWheel {}
        id: rows
        objectName: "homeRows"
        anchors.fill: parent
        anchors.leftMargin: Theme.pageMargin
        anchors.rightMargin: Theme.pageMargin
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

            StateMessage {
                objectName: "homeError"
                width: parent.width
                height: visible ? 200 : 0
                visible: page.home && page.home.errorText.length > 0
                kind: "error"
                description: page.home ? page.home.errorText : ""
                onRetry: if (page.home) page.home.reload()
            }

            Item {
                width: parent.width
                height: customize.implicitHeight
                ActionButton {
                    id: customize
                    objectName: "customizeHomeButton"
                    anchors.right: parent.right
                    iconSource: Icons.settings
                    text: qsTr("Customize home")
                    onClicked: Navigator.navigate("customizeHome")
                    KeyNavigation.down: libraries
                }
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

            // While the first rows load: grey cards where they will be.
            Column {
                objectName: "homeSkeleton"
                width: parent.width
                spacing: Theme.spacingLarge
                visible: page.home && page.home.loading && page.home.count === 0
                Repeater {
                    model: visible ? 2 : 0
                    Column {
                        width: parent.width
                        spacing: Theme.spacingSmall
                        SkeletonBox { width: 180; height: 20 }
                        Row {
                            spacing: Theme.spacing
                            Repeater {
                                model: 6
                                SkeletonBox {
                                    width: Theme.thumbWidth
                                    height: Math.round(Theme.thumbWidth / Theme.thumbAspect)
                                    radius: Theme.radiusLarge
                                }
                            }
                        }
                    }
                }
            }
        }

        delegate: CardRow {
            id: row
            required title
            required property string kind
            required property string sectionId
            required property var items
            required property string cardShape
            required property int index

            objectName: "homeRow_" + (kind === "recommend" || kind === "section" ? sectionId : kind)
            width: rows.width
            focus: ListView.isCurrentItem
            model: items
            cardHeight: Math.round(Theme.cardWidthFor(cardShape) / Theme.cardAspectFor(cardShape)) + 52
            delegate: ItemCard { shape: row.cardShape }
        }

        footer: Item { width: 1; height: Theme.spacingHuge }
    }

    StateMessage {
        objectName: "homeEmpty"
        anchors.centerIn: parent
        visible: page.home && !page.home.loading && page.home.count === 0
                 && page.home.libraries.count === 0 && page.home.errorText.length === 0
        kind: "empty"
        description: qsTr("Nothing here yet. Add media to a library on the server.")
    }
}
