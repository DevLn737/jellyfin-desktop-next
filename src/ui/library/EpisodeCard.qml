pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme
import Jfd.Ui.Controls
import Jfd.Ui.Shell
import Jfd.Ui.Library

// An episode in a series page's grid: the still with its number, length,
// watched check, progress and download state, a play mark on focus, then
// "3. Name" and two lines of the description. Clicking plays it.
T.ItemDelegate {
    id: card

    required property var model
    required property int index

    property real cardWidth: 300
    // The row of the first card of the page's range, so the fallback
    // number counts from the season's start.
    property int firstRow: 0
    // The episode's number in its season; its place in the list without one.
    readonly property string number: model.indexNumber !== undefined && model.indexNumber !== null
                                     ? String(model.indexNumber) : String(firstRow + index + 1)

    signal playClicked()

    implicitWidth: cardWidth
    implicitHeight: still.height + captions.implicitHeight + Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    padding: 0
    Accessible.name: number + ". " + (model.name || "")

    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()
    onClicked: playClicked()

    background: Item {}

    contentItem: Item {
        Rectangle {
            id: still
            width: card.cardWidth
            height: Math.round(width / Theme.thumbAspect)
            radius: Theme.radiusLarge
            color: Theme.surfaceRaised
            clip: true

            Image {
                anchors.fill: parent
                source: Library.image(card.model.imageUrl)
                // Decoded at the screen's pixels, in steps of 160, so a
                // resize does not reload every still.
                sourceSize.width: Math.ceil(still.width * Screen.devicePixelRatio / 160) * 160
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            // The number, top left.
            Rectangle {
                objectName: "episodeNumber"
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: Theme.spacingSmall
                width: Math.max(height, numberText.implicitWidth + 12)
                height: numberText.implicitHeight + 6
                radius: Theme.radiusSmall + 2
                color: Qt.alpha("#000000", 0.62)
                Text {
                    id: numberText
                    anchors.centerIn: parent
                    text: card.number
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.ExtraBold
                }
            }
            Rectangle {
                visible: card.model.played === true
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingSmall
                width: Theme.tvMode ? 34 : 24
                height: width
                radius: width / 2
                color: Theme.accent
                Icon {
                    anchors.centerIn: parent
                    source: Icons.check
                    color: Theme.accentText
                    size: parent.width - 8
                }
            }
            // The length, bottom right.
            Rectangle {
                readonly property string length: Library.formatRunTime(card.model.runTimeTicks || 0)
                visible: length.length > 0
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingSmall
                width: lengthText.implicitWidth + 12
                height: lengthText.implicitHeight + 4
                radius: Theme.radiusSmall + 2
                color: Qt.alpha("#000000", 0.62)
                Text {
                    id: lengthText
                    anchors.centerIn: parent
                    text: parent.length
                    color: Overlay.text
                    font.pixelSize: Theme.fontSizeSmall
                    font.weight: Font.Bold
                }
            }
            DownloadBadge {
                objectName: "episodeDownloadBadge"
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: Theme.spacingSmall
                size: 22
                downloadState: Downloads.revision >= 0 ? Downloads.stateOf(card.model.itemId || "") : ""
                progress: downloadState === "downloading"
                    ? (Downloads.progressRevision >= 0 ? Downloads.progressOf(card.model.itemId || "") : 0) : 0
            }
            // Play, on focus or under the pointer.
            Rectangle {
                anchors.centerIn: parent
                visible: card.hovered || card.visualFocus
                width: Theme.tvMode ? 72 : 52
                height: width
                radius: width / 2
                color: Qt.alpha(Overlay.text, 0.92)
                Icon {
                    anchors.centerIn: parent
                    source: Icons.play
                    color: "#101010"
                    size: parent.width * 0.45
                }
            }
            Rectangle {
                visible: (card.model.progress || 0) > 0
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                width: parent.width * (card.model.progress || 0)
                height: 4
                color: Theme.progress
            }
            // The focus ring inside the picture, as on the cards.
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                color: "transparent"
                border.width: card.visualFocus ? 3 : 0
                border.color: Theme.text
            }
        }

        Column {
            id: captions
            anchors.top: still.bottom
            anchors.topMargin: Theme.spacingSmall
            width: card.cardWidth
            spacing: 2
            Text {
                width: parent.width
                text: card.number + ". " + (card.model.name || "")
                color: Theme.text
                font.pixelSize: Theme.fontSizeBody + 1
                font.weight: Font.ExtraBold
                elide: Text.ElideRight
            }
            Text {
                width: parent.width
                visible: text.length > 0
                text: card.model.overview || ""
                color: Theme.textSecondary
                font.pixelSize: Theme.fontSizeSmall + 1
                lineHeight: 1.25
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
        }
    }
}
