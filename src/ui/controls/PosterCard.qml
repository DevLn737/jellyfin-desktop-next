import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A library item: image with title and subtitle below. `shape` picks the
// image's aspect ratio: "poster" (2:3, films and series), "thumb" (16:9,
// episodes and "continue watching"), "square" (album covers) or "circle"
// (artists: a square image cut round). `progress` (0..1)
// draws a watched bar; `played` a check mark; `unplayedCount` a badge.
T.AbstractButton {
    id: card
    // Colours of the nearest colour scope (the video player has its own).
    readonly property var colors: Theme.colorsFor(card)

    property string title
    property string subtitle
    property url imageSource
    property string shape: "poster"
    property real progress: 0
    property bool played: false
    property int unplayedCount: 0
    // The page colour a circle card's corners are cut out to.
    property color cutoutColor: colors.background

    // The image width; the card's height follows from the shape.
    property int cardWidth: Theme.cardWidthFor(shape)
    readonly property real imageAspect: Theme.cardAspectFor(shape)

    implicitWidth: cardWidth
    implicitHeight: frame.height + captions.implicitHeight + Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: subtitle.length > 0 ? title + ", " + subtitle : title

    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()

    Rectangle {
        id: frame
        width: card.cardWidth
        height: Math.round(card.cardWidth / card.imageAspect)
        radius: card.shape === "circle" ? width / 2 : Theme.radius
        color: colors.surfaceRaised
        clip: true
        scale: card.hovered || card.visualFocus ? 1.03 : 1
        Behavior on scale { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easing } }

        Image {
            id: image
            anchors.fill: parent
            source: card.imageSource
            sourceSize.width: card.cardWidth * 2
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            cache: true
            opacity: status === Image.Ready ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }
        }

        // Without an image: the title's initial on the placeholder.
        Text {
            anchors.centerIn: parent
            visible: image.status !== Image.Ready
            text: card.title.length > 0 ? card.title.charAt(0).toUpperCase() : ""
            color: colors.textDisabled
            font.pixelSize: Theme.fontSizeHeadline
        }

        // A circle card: a ring of the page colour over the square image
        // (Qt Quick's clip is a rectangle, whatever the radius). The frame
        // clips the ring's outer part.
        Rectangle {
            readonly property real ring: Math.ceil(frame.width * 0.22)
            visible: card.shape === "circle"
            x: -ring
            y: -ring
            width: frame.width + 2 * ring
            height: frame.height + 2 * ring
            radius: width / 2
            color: "transparent"
            border.width: ring
            border.color: card.cutoutColor
            antialiasing: true
        }

        Rectangle {
            visible: card.progress > 0 && card.progress < 1
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 4
            color: colors.scrim
            Rectangle {
                width: parent.width * Math.min(Math.max(card.progress, 0), 1)
                height: parent.height
                color: colors.progress
            }
        }

        Rectangle {
            visible: card.played || card.unplayedCount > 0
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.spacingSmall
            width: Math.max(height, badgeText.implicitWidth + Theme.spacingSmall * 2)
            height: 24
            radius: height / 2
            color: colors.accent
            Text {
                id: badgeText
                anchors.centerIn: parent
                visible: !card.played
                text: card.unplayedCount
                color: colors.accentText
                font.pixelSize: Theme.fontSizeSmall
                font.weight: Theme.fontWeightBold
            }
            Icon {
                anchors.centerIn: parent
                visible: card.played
                source: Icons.check
                size: 16
            }
        }

        FocusFrame { shown: card.visualFocus }
    }

    Column {
        id: captions
        anchors.top: frame.bottom
        anchors.topMargin: Theme.spacingSmall
        width: card.cardWidth
        spacing: 2

        Text {
            width: parent.width
            text: card.title
            color: colors.text
            font.pixelSize: Theme.fontSizeBody
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
        Text {
            width: parent.width
            visible: card.subtitle.length > 0
            text: card.subtitle
            color: colors.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            elide: Text.ElideRight
            horizontalAlignment: Text.AlignHCenter
        }
    }
}
