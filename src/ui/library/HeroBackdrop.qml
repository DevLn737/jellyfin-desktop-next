pragma ComponentBehavior: Bound

import QtQuick
import Jfd.Ui.Theme
import Jfd.Ui.Library

// The backdrop of the home page's banner (HeroBanner): the current featured
// item's picture on the right, fading into the page colour to the left and
// at the bottom, so the text over it stays readable. Items cross-fade; a
// picture is fetched only once its item has been current or is next.
Item {
    id: backdrop

    property var model: null
    property int current: 0
    readonly property color pageColor: Theme.background

    clip: true

    Repeater {
        id: pictures
        model: backdrop.model
        delegate: Image {
            id: picture
            required property int index
            required property var backdropUrl
            readonly property bool shown: index === backdrop.current
            // Fetched once current or next; kept after that.
            property bool wanted: false
            readonly property bool upcoming: shown || index === (backdrop.current + 1) % Math.max(1, pictures.count)
            onUpcomingChanged: if (upcoming) wanted = true
            Component.onCompleted: if (upcoming) wanted = true

            anchors.right: parent.right
            width: Theme.compact ? parent.width : parent.width * 0.78
            height: parent.height
            source: wanted && String(backdropUrl || "").length > 0 ? Library.image(backdropUrl) : ""
            sourceSize.width: 1600
            fillMode: Image.PreserveAspectCrop
            verticalAlignment: Image.AlignTop
            asynchronous: true
            opacity: shown && status === Image.Ready ? (Theme.compact ? 0.35 : 0.6) : 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Theme.durationLong; easing.type: Theme.easing } }
        }
    }

    // Into the page colour towards the text on the left...
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0.0; color: backdrop.pageColor }
            GradientStop { position: Theme.compact ? 0.0 : 0.22; color: Qt.alpha(backdrop.pageColor, Theme.compact ? 0.6 : 0.95) }
            GradientStop { position: 0.55; color: Qt.alpha(backdrop.pageColor, 0.45) }
            GradientStop { position: 1.0; color: Qt.alpha(backdrop.pageColor, 0.1) }
        }
    }
    // ... and at the bottom, into the rows.
    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.alpha(backdrop.pageColor, 0.35) }
            GradientStop { position: 0.3; color: Qt.alpha(backdrop.pageColor, 0.0) }
            GradientStop { position: 0.6; color: Qt.alpha(backdrop.pageColor, 0.15) }
            GradientStop { position: 1.0; color: backdrop.pageColor }
        }
    }
}
