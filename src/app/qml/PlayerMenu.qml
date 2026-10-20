import QtQuick
import QtQuick.Effects

// A player settings sheet over the video, styled after Jellyfin Web's action
// sheet: translucent, with the video behind it blurred.
Item {
    id: menu

    required property Item video
    property bool open: false
    // Measurement mode: slide in and out and move the highlight continuously.
    property bool animate: false
    property int current: 0

    readonly property var entries: [
        { title: "Аудио", value: "Японский, AAC 2.0" },
        { title: "Субтитры", value: "Русские (ASS)" },
        { title: "Скорость", value: "1x" },
        { title: "Качество", value: "Оригинал" },
        { title: "SyncPlay", value: "Группа: 3 участника" },
        { title: "Статистика", value: "" }
    ]

    width: 380
    x: open ? parent.width - width - 24 : parent.width + 8
    visible: x < parent.width
    Behavior on x { NumberAnimation { duration: 260; easing.type: Easing.OutCubic } }

    Timer {
        running: menu.animate
        repeat: true
        interval: 1200
        onTriggered: menu.open = !menu.open
    }
    Timer {
        running: menu.animate
        repeat: true
        interval: 200
        onTriggered: menu.current = (menu.current + 1) % menu.entries.length
    }

    // The video under the sheet, blurred like CSS backdrop-filter. Sampled at
    // half resolution: the blur hides the difference and saves fill rate.
    ShaderEffectSource {
        id: backdrop
        visible: false
        sourceItem: menu.video
        sourceRect: Qt.rect(menu.x, menu.y, menu.width, menu.height)
        textureSize: Qt.size(Math.max(1, menu.width / 2), Math.max(1, menu.height / 2))
        live: true
    }
    MultiEffect {
        anchors.fill: parent
        source: backdrop
        blurEnabled: true
        blur: 1.0
        blurMax: 48
        saturation: 0.1
        maskEnabled: true
        maskSource: shape
    }
    Rectangle {
        id: shape
        anchors.fill: parent
        radius: 12
        visible: false
        layer.enabled: true
    }

    Rectangle {
        anchors.fill: parent
        radius: 12
        color: Qt.rgba(0.11, 0.11, 0.11, 0.62)
        border.color: Qt.rgba(1, 1, 1, 0.08)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4

        Text {
            text: "Настройки воспроизведения"
            color: "white"
            font.pixelSize: 20
            font.weight: Font.Medium
            leftPadding: 12
            bottomPadding: 12
            topPadding: 8
        }

        Repeater {
            model: menu.entries
            delegate: Rectangle {
                required property var modelData
                required property int index
                width: parent.width
                height: 58
                radius: 8
                color: index === menu.current ? Qt.rgba(0, 0.64, 0.86, 0.85) : "transparent"
                Behavior on color { ColorAnimation { duration: 150 } }

                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: 14
                    Text { text: modelData.title; color: "white"; font.pixelSize: 17 }
                    Text {
                        visible: modelData.value.length > 0
                        text: modelData.value
                        color: Qt.rgba(1, 1, 1, 0.7)
                        font.pixelSize: 13
                    }
                }
                MouseArea {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: menu.current = index
                }
            }
        }
    }
}
