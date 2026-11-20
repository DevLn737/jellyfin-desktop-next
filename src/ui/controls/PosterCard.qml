import QtQuick
import QtQuick.Templates as T
import Jfd.Ui.Theme

// A library item: image with title and subtitle below. `shape` picks the
// image's aspect ratio: "poster" (2:3, films and series), "thumb" (16:9,
// episodes and "continue watching"), "square" (album covers) or "circle"
// (artists: a square image cut round). `progress` (0..1)
// draws a watched bar; `played` a check mark; `unplayedCount` a badge;
// `downloadState` and `downloadProgress` a DownloadBadge in the top left.
//
// With `quickActions` on, pointing at the card or focusing it shows a
// round Play in the middle (when `canPlay`) and Watched and Favorite
// buttons in the bottom right; `favorite` is the state that last one shows.
// They are for the pointer only (not focusable): from the keyboard or a
// gamepad, Enter plays or opens and the Menu key asks for the card's menu
// (`menuRequested`, also by a right click), which has the same actions.
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
    property string downloadState
    property real downloadProgress: 0
    property bool quickActions: false
    property bool canPlay: true
    property bool favorite: false
    readonly property bool quickShown: quickActions && (hovered || visualFocus)

    signal playRequested()
    signal playedToggled()
    signal favoriteToggled()
    // A right click or the Menu key; `at` is the point in the card.
    signal menuRequested(point at)

    // The page colour a circle card's corners are cut out to.
    property color cutoutColor: colors.background

    // The image width; the card's height follows from the shape.
    property int cardWidth: Theme.cardWidthFor(shape)
    readonly property real imageAspect: Theme.cardAspectFor(shape)

    implicitWidth: cardWidth
    implicitHeight: frame.height + captions.implicitHeight + Theme.spacingSmall
    focusPolicy: Qt.StrongFocus
    hoverEnabled: true
    Accessible.name: (subtitle.length > 0 ? title + ", " + subtitle : title)
                     + (downloadState === "complete" ? ", " + qsTr("downloaded") : "")

    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()
    Keys.onMenuPressed: (event) => {
        event.accepted = card.quickActions
        if (card.quickActions)
            card.menuRequested(Qt.point(card.width / 2, frame.height / 2))
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        enabled: card.quickActions
        onTapped: (eventPoint) => card.menuRequested(eventPoint.position)
    }

    Rectangle {
        id: frame
        width: card.cardWidth
        height: Math.round(card.cardWidth / card.imageAspect)
        radius: card.shape === "circle" ? width / 2 : Theme.radius
        color: colors.surfaceRaised
        clip: true
        // Only under the pointer: a focused card in the first place of a row
        // would grow past the row's clipped edge, ring and all.
        scale: card.hovered && !card.visualFocus ? 1.03 : 1
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
                color: colors.accentText
                size: 16
            }
        }

        DownloadBadge {
            objectName: "downloadBadge"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.margins: Theme.spacingSmall
            downloadState: card.downloadState
            progress: card.downloadProgress
        }

        // The quick actions: a shade, Play, Watched and Favorite. Made the
        // first time they show (a grid has hundreds of cards) and kept.
        Loader {
            id: quickLoader
            anchors.fill: parent
            active: false
            Connections {
                target: card
                function onQuickShownChanged() { if (card.quickShown) quickLoader.active = true }
            }
            sourceComponent: Component {
                Item {
                    id: quick
                    objectName: "quickActions"
                    visible: opacity > 0
                    opacity: card.quickShown ? 1 : 0
                    Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }

                    Rectangle {
                        anchors.fill: parent
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: Qt.rgba(0, 0, 0, 0.15) }
                            GradientStop { position: 1.0; color: Qt.rgba(0, 0, 0, 0.6) }
                        }
                    }

                    QuickButton {
                        objectName: "quickPlay"
                        anchors.centerIn: parent
                        visible: card.canPlay
                        diameter: Math.min(56, Math.round(frame.width * 0.32))
                        iconSource: Icons.play
                        filled: true
                        toolTipText: qsTr("Play")
                        onClicked: card.playRequested()
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: Theme.spacingSmall
                        anchors.bottomMargin: Theme.spacingSmall + (card.progress > 0 && card.progress < 1 ? 4 : 0)
                        spacing: Theme.spacingTiny
                        QuickButton {
                            objectName: "quickPlayed"
                            iconSource: Icons.check
                            on: card.played
                            toolTipText: card.played ? qsTr("Mark as not watched") : qsTr("Mark as watched")
                            onClicked: card.playedToggled()
                        }
                        QuickButton {
                            objectName: "quickFavorite"
                            iconSource: card.favorite ? Icons.favorite : Icons.favoriteBorder
                            on: card.favorite
                            toolTipText: card.favorite ? qsTr("Remove from favorites") : qsTr("Add to favorites")
                            onClicked: card.favoriteToggled()
                        }
                    }
                }
            }
        }

    }

    // Over the image (which clips its children), grown with it, and inside
    // its edge: a row of cards clips at its own edges too.
    FocusFrame {
        objectName: "cardFocusFrame"
        shown: card.visualFocus
        inside: true
        // Light on any poster, with the accent's halo inside.
        ringColor: colors.text
        anchors.fill: frame
        scale: frame.scale
        radius: frame.radius
    }

    // A round button on the image: dark glass, or the accent when `filled`
    // or `on`.
    component QuickButton: T.AbstractButton {
        id: quickButton
        readonly property var colors: Theme.colorsFor(quickButton)
        property url iconSource
        property string toolTipText
        property bool filled: false
        property bool on: false
        property int diameter: 32
        width: diameter
        height: diameter
        focusPolicy: Qt.NoFocus
        hoverEnabled: true
        Accessible.name: toolTipText
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: quickButton.filled || quickButton.on
                ? Qt.lighter(quickButton.colors.accent, quickButton.hovered ? 1.15 : 1)
                : Qt.rgba(0, 0, 0, quickButton.hovered ? 0.75 : 0.55)
            border.width: quickButton.filled || quickButton.on ? 0 : 1
            border.color: Qt.rgba(1, 1, 1, 0.25)
            scale: quickButton.down ? 0.94 : 1
        }
        Icon {
            anchors.centerIn: parent
            source: quickButton.iconSource
            size: Math.round(quickButton.diameter * 0.5)
            color: quickButton.filled || quickButton.on ? quickButton.colors.accentText : "white"
        }
        ToolTipLabel {
            text: quickButton.toolTipText
            shown: quickButton.hovered && !quickButton.down
        }
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
            font.weight: Font.Bold
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            visible: card.subtitle.length > 0
            text: card.subtitle
            color: colors.textSecondary
            font.pixelSize: Theme.fontSizeSmall
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
    }
}
