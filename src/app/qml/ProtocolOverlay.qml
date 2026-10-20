import QtQuick
import QtQuick.Effects

// The measurement overlay of architecture/measurement-protocol.md, identical
// in geometry and motion to the one the 2.x baseline injects as HTML/CSS:
// a 30%-wide full-height panel, rgba(0,0,0,0.5) over the video blurred
// (CSS blur(24px)), sliding from -100% to 0 and back once a second with
// ease-in-out; a 64 px spinner turning once a second; six lines of text.
Item {
    id: overlay

    required property Item video

    Item {
        id: panel
        width: Math.round(overlay.width * 0.3)
        height: overlay.height

        SequentialAnimation on x {
            loops: Animation.Infinite
            running: overlay.visible
            NumberAnimation { from: -panel.width; to: 0; duration: 500; easing.type: Easing.InOutQuad }
            NumberAnimation { from: 0; to: -panel.width; duration: 500; easing.type: Easing.InOutQuad }
        }

        // The visible part of the panel samples the video under it at full
        // resolution (no downscaling shortcut, to stay comparable with
        // Chromium's backdrop-filter). MultiEffect does not render a hidden
        // ShaderEffectSource, so the copy stays visible under the blur.
        ShaderEffectSource {
            id: backdrop
            x: Math.max(0, -panel.x)
            width: panel.width - x
            height: panel.height
            sourceItem: overlay.video
            sourceRect: Qt.rect(panel.x + x, 0, width, height)
            live: true
        }
        MultiEffect {
            x: backdrop.x
            width: backdrop.width
            height: backdrop.height
            source: backdrop
            autoPaddingEnabled: false
            blurEnabled: true
            blur: 1.0
            blurMax: 48
        }
        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(0, 0, 0, 0.5)
        }
        Column {
            anchors.fill: parent
            anchors.margins: 40
            spacing: 24
            Repeater {
                model: ["Главная", "Фильмы", "Сериалы", "Музыка", "Избранное", "Настройки"]
                Text { required property string modelData; text: modelData; color: "white"; font.pixelSize: 24 }
            }
        }
    }

    Rectangle {
        width: 64
        height: 64
        anchors.centerIn: parent
        radius: 32
        color: "transparent"
        border.width: 6
        border.color: Qt.rgba(1, 1, 1, 0.25)
        // The accent quarter of the ring, like border-top-color in the CSS.
        Rectangle {
            width: 6; height: 22; radius: 3
            x: parent.width / 2 - 3; y: 0
            color: "#00a4dc"
        }
        RotationAnimation on rotation { from: 0; to: 360; duration: 1000; loops: Animation.Infinite; running: overlay.visible }
    }
}
